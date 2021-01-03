#!/bin/sh
# empty lines are unchanged
# html comment tags are removed, so the comment is the c code
# for html lines backslashes are escaped, double quotes are escaped, leading whitespaces are removed, this is then enclosed in an mg_printf
perl -pe '/^\s*$/ or s/<!--(.*)-->/\1/ or do {s/\\/\\\\/g; s/\"/\\\"/g; s/^\s*// ;chomp; $_="mg_printf(conn, \"$_\\n\");\n"}' $@
