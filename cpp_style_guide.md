# C++ Style Guide
## Credit
This style guide is heavily inspired by the [Google C++ Style Guide](google.github.io/styleguide/cppguide.html) 
and occasionally the [Linux Kernel Style Guide](https://www.kernel.org/doc/html/v4.10/process/coding-style.html).

## Code Lines
Lines should only be up to 100 characters. The standard appears to be 80, but the Vulkan library is
 very long. However, if it would look more readable at a shorter length, that is acceptable.

# Header Files
Header files **should** use the extension .hpp to avoid confusion with C headers.

All .cpp files **must** have a corresponding header file, except for main.cpp.

All headers **must**:
- have a #define guard (using ifndef, not pragma-once)
- only include what's needed in the header
- avoid forward declarations (extern functions)

### Defining Functions in Header Files
Include the definition of a function in the header only if the whole function fits on a single line.

# Scoping
## Namespaces
Put everything in a namespace. Every namespace should be uniquely named based on 
the project name and/or path.
- Code that does not belong to the API should be put in an _internal_ namespace
- Nested namespaces are preferred as single-line declarations

## Internal Linkage
Put any code in a .cpp file in a blank namespace if it does not need to be referenced 
outside the file.

## Nonmember and Global Functions
Place non-member functions in a namespace and avoid completely global functions.

## Local Variables
Put variables in the smallest possible scope.

## Static and Global Variables
**DO NOT** use static or global variables. Use constexpr or constants instead.

## thread_local Variables
Don't.

# Classes
## Constructors
Constructors **must**:
- be declared with _noexcept_
- fully construct the object

## Destructors
Destructors **must**:
- be declared with _noexcept_
- only fail on a fatal error; any other error must be dealt with properly

## Implicit Conversions
**Avoid** implicit conversions.

## Copyable and Movable
Classes **should** define the copy and move operators; they may use _default_ and _delete_ to 
simplify the definition.

```c++
class Copyable {
    Copyable(const Copyable&) = default;
    Copyable& operator=(const Copyable&) = default;
};

class MoveOnly {
    MoveOnly(MoveOnly&&) = default;
    MoveOnly& operator=(MoveOnly&&) = default;
};

class NoCopyOrMove {
    NoCopyOrMove(const NoCopyOrMove&) = delete;
    NoCopyOrMove& operator=(const NoCopyOrMove&) = delete;
};
```

## Access Control
All data members **must** be _private_.

## Declaration Order
Sections **must** be ordered _public_, _protected_, then _private_.

Within each section:
- Types and type aliases
- Static constants
- Factory functions
- Constructors and assignment operators
- Destructor
- Getters and setters
- All other functions
- All other data members

**Note**: types and type aliases (_using_ or _typedef_) **should** be declared before all sections 
at the very top of the class definition.

# Functions
## Inputs and Outputs
A function's output **must** be given as a return value or struct, rarely as in/out parameters.
- If a functions has to use in/out parameters, it **must** label the parameters as in or out

## Length
Mostly agree with the [Linux Kernel Style Guide](https://www.kernel.org/doc/html/v4.10/process/coding-style.html#functions). 
That is, a function should only be at most 50 lines and have a single function and do it well.

If the function is really simply but really long, like a switch statement, then it is acceptable. 
If it is super complex and difficult to understand then break it into multiple functions.

## A Function's Purpose
As stated above, a function **must** only have one job. It **must** also do the same thing 
if the same values are passed in. That is, a functino **must not** change how it runs if the 
exact same values are provided.

## Local Variables
Keep the number of local variables below 5-10 and they **must** be declared at the start of 
the function. This forces the programmer to show what data is needed to fulfill the task 
and ensures a simple function.

However, some structs in the Vulkan library require too many reference pointers. In that case, 
more than ten variables is acceptable.

## Exiting a Function
A function **must** use _return_ to exit the function. This bans the usage of _goto_ statements.

## Overloading
If a function is overloaded, it **must** be obvious which overload is being called. 

## Default Arguments
Similar to function overloading, it **must** be obvious what the default is. 
If need be, explain it in the function comment. 

Read more in [Google's style guide](https://google.github.io/styleguide/cppguide.html#Default_Arguments).

## Trailing Return Type
Use if and only if it makes the function definition more readable.

# Naming
## Variables
Local variables **should** be kept short; global variables **must** be descriptive. 
Common variable names:
- Loops: i, j, k, etc.
- Temporary variables: tmp

## Functions
A function's name **must** be descriptive, yet concise. It should not need 
a comment for the user to know what it does.

# Miscellaneous
## Ownership and Smart Pointers
Pointers **must** be smart pointers. Raw pointers are bad.

## Comments
Avoid putting comments inside a function. Long comments should follow the 
[Linux Kernel method](https://www.kernel.org/doc/html/v4.10/process/coding-style.html#commenting):

```cpp
/*
 * Behold! A comment.
 *
 * Commenting like this makes the code look more professional.
 */
```