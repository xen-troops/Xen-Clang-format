/*
This is used to check if includes are sorted or not.
A space afer include keyword
*/
#include <unistd.h>
#include <stdio.h>
#define true 1

/*
This will test brackets.
Placed in a line of their own.
*/
struct sample
{
    int a;
};

int main()
{
    /*
    This will test indentation
    Comments are aligned wrt blocks.
    Indent Using spaces.
    Indent level equals 4 spaces.
    Code within blocks is indented by one extra indent level.
    The enclosing braces of block are indented the same as code outside block.
    Space between keyword and parentheses and condition.
    */
    if ( true )
    {
        if ( true )
        {
            if ( true )
            {
                int a = 32;
            }
        }
    }
    /*
    Spaces around binary operators.
    No spaces around structure access operators, '.' and '->
    */
    int x = 1 + 2 - 3 * 4 / 5 % 6;
    struct sample ob;
    struct sample* ob1;
    ob.a = 10;
    ob1->a = 10;
    /*
    Check for break in template declarations
    */
    template <class P>
    void updateMenuList( P*, struct menu* );
    return 0;
}