// 
// Very simple mathematical expression (in lisp syntax) evaluator with JIT code generation.
// 
// Objectives:
// 
//  - Demonstrate the use of the AsmJit library with a simple example.
//  - Demonstrate the benefits of JIT code generation vs interpreted code with a simple example.
// 

#include <vector>
#include <list>
#include <iostream>
#include <map>
#include <functional>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <stdexcept>
#include <chrono>

#include <asmjit/asmjit.h>


// S-Expression structure.
struct Cell{
    enum Type {Symbol, Number, List};
    typedef Cell (*proc_type)(const std::vector<Cell> &);
    typedef std::vector<Cell>::const_iterator iter;
    Type type;
    std::string val;
    std::vector<Cell> list;
    Cell(Type type = Symbol) : type(type) {}
    Cell(Type type, const std::string & val) : type(type), val(val) {}
};


// Generic templated visitor base class.
// (Probably not the best "design" wise, but it keeps things nice and 
//  concise - I want to highlight the small difference between the
//  interpreter and JIT versions.)
template <typename EvalReturn> class Visitor{
public:
    typedef std::map<std::string, std::function<EvalReturn (const std::vector<EvalReturn> &)>> 
        FunctionMap;

    typedef std::function<EvalReturn (const std::string &symbol)> SymbolHandler;
    typedef std::function<EvalReturn (const std::string &number)> NumberHandler;

protected:
    FunctionMap functionMap;
    NumberHandler numberHandler;
    SymbolHandler symbolHandler;

public:
    Visitor(){
    }

    EvalReturn eval(const Cell &c){
        switch(c.type){
            case Cell::Number:{
                return numberHandler(c.val.c_str());
            }case Cell::List:{
                std::vector<EvalReturn> evalArgs(c.list.size()-1);
                
                // eval each argument
                std::transform(c.list.begin()+1, c.list.end(), evalArgs.begin(), 
                    [=](const Cell &c) -> EvalReturn{
                    return this->eval(c);
                });

                if(functionMap.find(c.list[0].val) == functionMap.end())
                    throw std::runtime_error("Could not handle procedure: " + c.list[0].val);

                // call function specified by symbol map with evaled arguments
                return functionMap.at(c.list[0].val)(evalArgs);
          }case Cell::Symbol:{
              if(symbolHandler)
                  return symbolHandler(c.val);
              else
                  std::runtime_error("Cannot handle symbol: " + c.val);
          }
        }
      std::runtime_error("Should never get here.");
      return EvalReturn(); // quiet compiler warning.
    }
};

// Interpreted calculator without variables (no symbolHandler!)
class Calculator : public Visitor<double>{
public:
    Calculator(){
        // standard functions
        functionMap["+"] = [](const std::vector<double> &d){return d[0] + d[1];};
        functionMap["-"] = [](const std::vector<double> &d){return d[0] - d[1];};
        functionMap["/"] = [](const std::vector<double> &d){return d[0] / d[1];};
        functionMap["*"] = [](const std::vector<double> &d){return d[0] * d[1];};

        numberHandler = [](const std::string &number){
            return std::atof(number.c_str());
        };
    }
};

// Extend calculator above into function evaluator.
class CalculatorFunction : public Calculator{
private:
    std::map<std::string, int> argNameToIndex;
    Cell cell;
public:
    CalculatorFunction(const std::vector<std::string> &names, const Cell &c) : cell(c){
        for(size_t i = 0; i < names.size(); ++i)
            argNameToIndex[names[i]] = i;
    }

    double operator()(const std::vector<double> &args){
        symbolHandler = [&](const std::string &name) -> double{
            return args[this->argNameToIndex[name]];	
        };
        return eval(cell);
    }
};


// JIT version of CalculatorFunction class.
// Expressions return AsmJit SSE "registers"/variables.
class CodeGenCalculatorFunction : public Visitor<AsmJit::XmmVar>{
private:
    AsmJit::X86Compiler compiler;
    std::map<std::string, int> argNameToIndex;

    typedef double (*FuncPtrType)(const double * args);
    FuncPtrType generatedFunction;
public:
    CodeGenCalculatorFunction(const std::vector<std::string> &names, const Cell &cell){
        using namespace AsmJit;

        // Map operators to assembly instructions
        functionMap["+"] = [&](const std::vector<XmmVar> &args) -> XmmVar{
            compiler.addsd(args[0], args[1]);
            return args[0];
        };

        functionMap["-"] = [&](const std::vector<XmmVar> &args) -> XmmVar{
            compiler.subsd(args[0], args[1]);
            return args[0];
        };

        functionMap["*"] = [&](const std::vector<XmmVar> &args) -> XmmVar{
            compiler.mulsd(args[0], args[1]);
            return args[0];
        };

        functionMap["/"] = [&](const std::vector<XmmVar> &args) -> XmmVar{
            compiler.divsd(args[0], args[1]);
            return args[0];
        };

        // Convert numbers into AsmJit vars.
        numberHandler = [&](const std::string &number) -> XmmVar{
            double x = std::atof(number.c_str());
            XmmVar xVar(compiler.newXmmVar());
            SetXmmVar(compiler, xVar, x);
            return xVar;
        };

        for(size_t i = 0; i < names.size(); ++i)
            argNameToIndex[names[i]] = i;

        symbolHandler = [&](const std::string name) -> XmmVar{
            // Lookup name in args and return AsmJit variable
            // with the arg loaded in.
            // TODO: this could be more efficient - could
            // create one list of XmmVars and use that.
            GpVar ptr(compiler.getGpArg(0));
            XmmVar v(compiler.newXmmVar());
            int offset = argNameToIndex.at(name)*sizeof(double);
            compiler.movsd(v, Mem(ptr, offset));
            return v;
        };

        generatedFunction = generate(cell);
    }

    FuncPtrType generate(const Cell &c){
        compiler.newFunc(AsmJit::kX86FuncConvDefault, 
                AsmJit::FuncBuilder1<double, const double *>());
        AsmJit::XmmVar retVar = eval(c);
        compiler.ret(retVar);
        compiler.endFunc();
        return reinterpret_cast<FuncPtrType>(compiler.make());

    }

    double operator()(const std::vector<double> &args) const {
        return generatedFunction(&args[0]); 
    }

    ~CodeGenCalculatorFunction(){
        AsmJit::MemoryManager::getGlobal()->free((void*)generatedFunction);
    }

private:
    void SetXmmVar(AsmJit::X86Compiler &c, AsmJit::XmmVar &v, double d){
        using namespace AsmJit;
        // No immediates for SSE regs/doubles. So put into a general purpose reg
        // and then move into SSE - we could do better than this.
        GpVar gpreg(c.newGpVar());
        uint64_t *i = reinterpret_cast<uint64_t*>(&d);
        c.mov(gpreg, i[0]); 
        c.movq(v, gpreg); 
        c.unuse(gpreg);
    }

};


// Convert given string to list of tokens.
// originally from: 
// http://howtowriteaprogram.blogspot.co.uk/2010/11/lisp-interpreter-in-90-lines-of-c.html
std::list<std::string> tokenize(const std::string & str){
    std::list<std::string> tokens;
    const char * s = str.c_str();
    while (*s) {
        while (*s == ' ')
            ++s;
        if (*s == '(' || *s == ')')
            tokens.push_back(*s++ == '(' ? "(" : ")");
        else {
            const char * t = s;
            while (*t && *t != ' ' && *t != '(' && *t != ')')
                ++t;
            tokens.push_back(std::string(s, t));
            s = t;
        }
    }
    return tokens;
}

// Numbers become Numbers; every other token is a Symbol.
// Originally from: 
// http://howtowriteaprogram.blogspot.co.uk/2010/11/lisp-interpreter-in-90-lines-of-c.html
Cell atom(const std::string & token)
{
    if (std::isdigit(token[0]) || (token[0] == '-' && std::isdigit(token[1])))
        return Cell(Cell::Number, token);
    return Cell(Cell::Symbol, token);
}

// Return the s-expression in the given tokens.
// Originally from: 
// http://howtowriteaprogram.blogspot.co.uk/2010/11/lisp-interpreter-in-90-lines-of-c.html
Cell readFrom(std::list<std::string> & tokens)
{
    const std::string token(tokens.front());
    tokens.pop_front();
    if (token == "(") {
        Cell c(Cell::List);
        while (tokens.front() != ")")
            c.list.push_back(readFrom(tokens));
        tokens.pop_front();
        return c;
    }
    else
        return atom(token);
}

// Return the Lisp expression represented by the given string.
// Originally from: 
// http://howtowriteaprogram.blogspot.co.uk/2010/11/lisp-interpreter-in-90-lines-of-c.html
Cell read(const std::string & s)
{
    std::list<std::string> tokens(tokenize(s));
    return readFrom(tokens);
}

int main (int argc, char *argv[])
{
    if(argc <= 2){
        std::cout << "Error: Not enough arguments.\n";
        std::cout << "Usage: \n\n   $ calc \"((args1 ... argsn) (expr))\" arg1 ... argn\n\n"; 
        std::cout << "Example: \n\n   $ calc \"((x y) (+ (* x y) 10.5))\" 4 2\n\n"; 
        std::cout << "Use the \"-benchmark\" switch to bechmark interpreted vs JIT evaluation.\n";
        return 0;
    }


    size_t codeIndex = 1;
    bool benchmark = false;
    if(std::string(argv[1]) == "-benchmark"){
        codeIndex++;
        benchmark = true;
    }


    // Parse first command line argument.
    Cell cell = read(argv[codeIndex]); 
    if(!(cell.type == Cell::List && cell.list.size() == 2 &&
                cell.list[0].type == Cell::List &&
                (cell.list[1].type == Cell::List || cell.list[1].type == Cell::Symbol))){
        std::cout << "Error: Function cell must be of form ((arg1 arg2 ...) (expression))\n";
        return 0;
    }

    const Cell &argsCell = cell.list[0]; // First cell is list of arguments.
    const Cell &expr = cell.list[1]; // Second is the code.

    // Load function argument names.
    std::vector<std::string> argNames;
    for(Cell c : argsCell.list){
        if(c.type == Cell::Symbol){
            argNames.push_back(c.val);
        }else{
            std::cout << "Error: Function cell must be of form ((arg1 arg2 ...) (expression))\n";
            return 0;
        }
    }

    // Read numeric arguments from command line.
    if(codeIndex + 1 + argNames.size() != size_t(argc)){
        std::cout << "Error: Wrong number of numeric arguments passed in.\n";
        return 0;
    }

    std::vector<double> numericArgs;
    for(size_t i = codeIndex + 1; i < size_t(argc); ++i)
        numericArgs.push_back(std::atof(argv[i]));


    // Run the code
    namespace sc = std::chrono;
    CalculatorFunction interpretedFunction(argNames, expr);
    CodeGenCalculatorFunction jitFunction(argNames, expr);
    std::cout << "Interpreted output: " << interpretedFunction(numericArgs) << std::endl;
    std::cout << "Code gen output: " << jitFunction(numericArgs) << std::endl;


    if(benchmark){
        std::cout << "\nBenchmarking...\n";
        size_t repetitions = 10000000;
        auto startInterp = sc::high_resolution_clock::now();
        for(size_t i = 0; i < repetitions; ++i)
            interpretedFunction(numericArgs);
        auto endInterp = sc::high_resolution_clock::now();

        auto startJit = sc::high_resolution_clock::now();
        for(size_t i = 0; i < repetitions; ++i)
            jitFunction(numericArgs);
        auto endJit = sc::high_resolution_clock::now();

        std::cout << "Duration for " << repetitions << " repeated evaluations:\n\n";
        std::cout << " - Interpreted: " << 
                     sc::duration_cast<sc::milliseconds>(endInterp-startInterp).count() << "ms\n";

        std::cout << " - JIT: " << 
                     sc::duration_cast<sc::milliseconds>(endJit-startJit).count() << "ms \n";
    }

    return 0;
}

