*This README is about Derivgrind; see [`README_ORIGINAL`](README_ORIGINAL)
for information about Valgrind.*

# Derivgrind

Derivgrind is an automatic differentiation tool applicable to compiled programs.
It has been implemented in the [Valgrind](https://valgrind.org/)
framework for building dynamic analysis tools. 

For more information beyond this [README.md](README.md), you may have a look at our papers:
- M. Aehle, J. Blühdorn, M. Sagebaum, N. R. Gauger: *Forward-Mode Automatic Differentiation of Compiled Programs*. [arXiv:2209.01895](https://arxiv.org/abs/2209.01895), 2022.
- M. Aehle, J. Blühdorn, M. Sagebaum, N. R. Gauger: *Reverse-Mode Automatic Differentiation of Compiled Programs*. [arXiv:2212.13760](https://arxiv.org/abs/2212.13760), 2022.

## Building Derivgrind
Make sure you have the required tools, headers and libraries, which are listed in
[environment.def](environment.def). You may run a Singularity image built from 
this file to reproducibly obtain an environment that contains all dependencies.

Clone this repository with `git clone --recursive`, and run the following commands in the root directory: 

    ./autogen.sh
    ./configure --prefix=$PWD/install
    make install

## Running Testcases

In `derivgrind/diff_tests`, run something like

    python3 run_tests.py dot_amd64_gcc_double_addition

The names of the unit tests are composed of an AD mode, architecture, language/compiler, floating-point type and 
arithmetic formula. You may use `*` as a wildcard to run several tests at once. You may specify the 
Derivgrind installation directory with `--prefix=path`, and a temporary directory with `--tempdir=path`.

## Differentiating a Simple C++ Program in Forward Mode
Compile a simple C++ "client" program from 

    // forward.cpp:
    
    #include <iostream>
    #include <valgrind/derivgrind.h>

    int main(){
      double x, x_d = 1;
      std::cin >> x;
      DG_SET_DOTVALUE(&x,&x_d,sizeof(double));
      double y = x*x*x, y_d;
      DG_GET_DOTVALUE(&y,&y_d,sizeof(double));
      std::cout << "f(" << x << ") = " << y << std::endl;
      std::cout << "df/dx(" << x << ") = " << y_d << std::endl;
    }

via `g++ forward.cpp -o forward -Iinstall/include` and run it via
   
    install/bin/valgrind --tool=derivgrind ./forward

Enter an input value `x` on the command line (e.g. `4.0`), and see whether 
the derivative computed by Derivgrind matches the analytic derivative
`3*x*x` (e.g. `48.0`).

The two preprocessor macros seed `x` and obtain the dot value from `y`, 
respectively, via Valgrind's client request mechanism. Access to the client 
program's source code is only necessary to identify input and output variables
in this way. In between, the client program may,  e. g., make calls to closed-source 
libraries.

## Differentiating a Simple C++ Program in Reverse Mode
Compile another simple C++ "client" program from

    // reverse.cpp:
    
    #include <iostream>
    #include <valgrind/derivgrind.h>

    int main(){
      double x;
      std::cin >> x;
      DG_INPUTF(x);
      double y = x*x*x;
      DG_OUTPUTF(y);
    }

via `g++ reverse.cpp -o reverse -Iinstall/include` and run it via
   
    install/bin/valgrind --tool=derivgrind --record=$PWD ./reverse

Enter an input value `x` on the command line (e.g. `4.0`). Once the program
finishes, you should find a binary file `dg-tape` storing the recorded tape,
and text files `dg-input-indices`, `dg-output-indices` storing identifiers for
`x` and `y`, respectively. Create a text file `dg-output-bars` containing 
`1.0` and run 

    install/bin/tape-evaluation $PWD
    
This stores the reverse-mode automatic derivative in a text file `dg-input-bars`. 
If you have multiple input or output variables, their bar values should appear 
in the respective text files as separate lines, in the same order in which they
are declared.

To evaluate the tape in forward direction, create a text file `dg-input-dots` containing
the dot values of the inputs, and call `tape-evaluation $PWD --forward`. This will
store the dot values of the outputs in `dg-output-dots`. This alternative way
of computing forward-mode derivatives can be more efficient than the way presented above,
if you have many input variables.

## License
Derivgrind is licensed under the GNU General Public License, version 2. 
Read the file [COPYING](COPYING) in the source distribution for details.

Some parts of Derivgrind might end up being permanently inserted into 
the client program, like the `valgrind/derivgrind.h` header providing the client
request macros. Generally, we have put such files under the more permissive
MIT license.

## Limitations
- While Valgrind supports many more platforms, only X86/Linux and AMD64/Linux 
  are supported by Derivgrind at the moment.
- Machine code can "hide" real arithmetic behind integer or logical instructions 
  in manifold ways. For example, a bitwise logical "and" can be used to set the
  sign bit to zero, and thereby compute the absolute value. Derivgrind recognizes only
  the most important of these constructs. Thus, avoid direct manipulation 
  of a floating-point number's binary representation in your program, and avoid 
  differentiation of highly optimized numerical libraries.
- Valgrind might not know all the instructions used in your program, and makes 
  some floating-point operations behave slightly differently than they do outside
  of Valgrind.
- Running a program under Derivgrind will significantly slow it down. For our 
  Burgers' PDE benchmark compiled in release mode, we have measured a factor of 
  around 30 in forward mode and 180 for the tape recording, respectively.

More details on limitations can be found in our [forward-mode paper](https://arxiv.org/abs/2209.01895).
  

