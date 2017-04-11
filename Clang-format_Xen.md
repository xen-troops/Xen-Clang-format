# Xen Hypervisor format features

This document contains the format specification of the Xen Hypervisor and the level of implementation which is possible using clang-format tool.
## Indentation
 ### Tasks done
  - Indent Using spaces.
  - Indent level equals 4 spaces.
  - Code within blocks is indented by one extra indent level.
  - The enclosing braces of a block are indented the same as the code _outside_ the block. 

## Whitespaces
### Tasks done
  - Spaces around binary operators.
  - No spaces around structure access operators, '.' and '->
### Tasks without support
  - Removal of trailing whitespaces.
### Tasks with partial support
  - Spread out logical statements. There is suppport to add spaces before and after parenthesis, but doing this we cannot limit spacing to keywords. It will be included in every parentheses.
  - Spaces placed between the keyword and the brackets surrounding the condition, between the brackets and the condition itself.

## Line length
### Tasks done
  - Lines should be less than 80 characters in length.
### Tasks without support
  - Trailing portions of line broken must be indented.
  - User visible strings (e.g., printk() messages) should not be split.

## Brackets
### Tasks done
  - Placed on a line of their own.
### Tasks without support
  - Exception for do while loop.
  - Braces should be omitted for blocks with a single statement.
## Comments
### Tasks without support
  - Only C style /* ... */ comments are to be used.
  - Multi-word comments should begin with a capital letter.
  -  Comments containing a single sentence may end with a full stop.
  -  Comments containing several sentences must have a full stop after each sentence.
  -  Multi-line comment blocks should start and end with comment markers on separate lines and each line should begin with a leading '*'. 
## Emacs local variables
  - No support
