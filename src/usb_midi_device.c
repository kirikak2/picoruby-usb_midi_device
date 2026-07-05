/*
 * PicoRuby USB-MIDI Device - VM selection wrapper
 */

#include "../include/usb_midi_device.h"

#if defined(PICORB_VM_MRUBY)
  #include "mruby/usb_midi_device.c"
#elif defined(PICORB_VM_MRUBYC)
  #include "mrubyc/usb_midi_device.c"
#endif
