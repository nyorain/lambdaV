# λV to SPIR-V compiler (proof of concept)

Final project for CPSC 539B: Compiler theory. This models a compiler from
a simple made-up programming language with functional concepts (developed
as far as technically possible) to the SPIR-V shader assembly language.
The main task for the project wasn't even the compiler but the formal
model and proof found in [report/](report/).

SPIR-V is too restricted to execute everything that can
be specified in a functional language (no dynamic dispatch,
no memory allocation, no call-stack, no recursion allowed) so the idea
was to see if we can model functional elements (especially getting as close to
first-class function values as possible) in alternative ways, obviously
imposing limitations on the source language.
