#! /bin/sh

# Certain types can only safely be accessed through pointers, not C++ references.
# This is due to their being variably-sized. <rdar://91434355>
# Find code that uses references to the problem types.

SearchDir="$1"

if [ -z "$SearchDir" ] ; then
	SearchDir=.
fi

egrep -r "(AURenderEvent|MIDI(Packet|Event)List)\s*(const\s*)?&" "$SearchDir"
if [ $? -eq 0 ]; then
  echo "error: forming reference to a type which causes UB <rdar://91434355>"
  exit 1 
fi
