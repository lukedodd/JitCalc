JitCalc
=======

A small program demonstrating how to use [AsmJit](http://code.google.com/p/asmjit/) to create a just-in-time mathematical expression evaluator. Further description of JitCalc and AsmJit [can be found in this blog post](http://www.lukedodd.com/?p=337).


The objectives are to demonstrate how to use AsmJit (right now there are few examples online) and to demonstrate how much faster JIT code generation can be.

For a more serious expression evaulator please see [mathspresso](https://code.google.com/p/mathpresso/).

Compiling
---------

This project uses a CMake build. You'll need a recent version of GCC or clang (C++11 features used).

    # git checkout
    git clone https://github.com/lukedodd/JitCalc.git
    # make build directory
    mdkir build-jitcalc
    cd build-jitcalc/
    # run cmake and compile
    cmake ../JitCalc
    make
    # run an example
    ./jitcalc "((x y) (+ x (/ y 2)))" 5 20.5
    
Tested on x86-64 Linux only. I would expect x86-64 OSX to work too. Windows (visual studio 2010+) might just work (perhaps requring small tweaks) but it has not been tested yet, the same is true of 32bit.
    
Usage
-----


Expressions are lisp like. Operations supported are addition, subtraction, multiplication and division (+, -, *, /).

Code should be supplied to the jitcalc command in the first argument. An S-Expression of this form is expected `((args...) (expr))`. Subsequent command line arguments are bound to the function arguments of the supplied expression. Some examples should make this clear!

    $ ./jitcalc "((x) (+ x 10))" 5 # add 10 to x, which is bound to 5
    Interpreted output: 15
    Code gen output: 15

    $ ./jitcalc "((x y) (+ x y))" 100 1 # add two arguments
    Interpreted output: 101
    Code gen output: 101

    $ ./jitcalc "((x y) (+ x (* y 2)))" 2 2.5 # multiply second argument by 2 and add to first argument
    Interpreted output: 7
    Code gen output: 7

    $ # more complex expression
    $ ./jitcalc "((x y) (+ (* (+ x 20) y) (/ x (+ y 1))))" 1 10
    Interpreted output: 210.091
    Code gen output: 210.091

Benchmark Results
-----------------

Here are benchmark command examples and results.

    $ ./jitcalc -benchmark "((x y) (* x (+ y 10)))" 5 10
    Interpreted output: 100
    Code gen output: 100

    Benchmarking...
    Duration for 10000000 repeated evaluations:

     - Interpreted: 2532ms
     - JIT: 19ms 

    $ ./jitcalc -benchmark "((x y) (+ (* (+ x 20) y) (/ x (+ y 1))))" 15.5 20 
    Interpreted output: 710.738
    Code gen output: 710.738

    Benchmarking...
    Duration for 10000000 repeated evaluations:

     - Interpreted: 5732ms
     - JIT: 52ms
