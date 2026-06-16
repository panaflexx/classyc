/* This file is a part of MIR project.
   Copyright (C) 2020-2024 Vladimir Makarov <vmakarov.gcc@gmail.com>.
*/

/* C23 semantics: true/false are _Bool-typed constants.
   This lets dict literals emit dict_create_bool() instead of
   dict_create_int64() for proper JSON true/false serialization. */
static char stdbool_str[]
  = "#ifndef __STDBOOL_H\n"
    "#define __STDBOOL_H\n"
    "\n"
    "#define bool _Bool\n"
    "#define true ((_Bool)1)\n"
    "#define false ((_Bool)0)\n"
    "#define __bool_true_false_are_defined 1\n"
    "#endif /* #ifndef __STDBOOL_H */\n";
