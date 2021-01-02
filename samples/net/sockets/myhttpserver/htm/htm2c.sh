#!/bin/sh
perl -pe '/^\s*$/ or s/<!--(.*)-->/\1/ or do {s/\"/\\\"/g; s/\\n/\\\\n/g; s/^\s*// ;chomp; $_="mg_printf(conn, \"$_\\n\");\n"}' $@
