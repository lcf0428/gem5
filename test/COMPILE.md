g++ side_channel.cpp -O2 -fno-builtin-memcpy \
  -I../include \
  -L../util/m5/build/x86/out \
  -lm5 \
  -o side_channel
