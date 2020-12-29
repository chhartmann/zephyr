#!/bin/sh
perl -pe '/^\s*$/ or s/<!--(.*)-->/\1/ or do {s/\"/\\\"/g; chomp; $_="mg_printf(conn, \"$_\\n\");\n"}' $@
