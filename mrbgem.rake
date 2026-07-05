MRuby::Gem::Specification.new('picoruby-usb_midi_device') do |spec|
  spec.license = 'MIT'
  spec.author  = 'Toshio Maki'
  spec.summary = 'USB-MIDI Device transport layer'
  spec.require_name = 'usb_midi_device'
  spec.add_dependency 'picoruby-machine'
end
