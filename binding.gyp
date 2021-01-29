{
  "targets": [
  {
    "target_name": "audio",
    "sources": [ "audio-napi.cc" ],
    "cflags" : [ "-lole32", "-loleaut32"],
    "conditions": [
      ['OS=="mac"',
      {
        'defines': [
          '__MACOSX_CORE__'
        ],
        'link_settings': {
          'libraries': [
            '-framework', 'CoreAudio',
          ]
        },
        'xcode_settings': {
          'GCC_ENABLE_CPP_EXCEPTIONS': 'YES'
        }
      }
      ],
    ]
  },
  {
    'target_name': 'action_after_build',
    'type': 'none',
    'dependencies': [ 'audio' ],
    'copies': [
      {
        'files': [ '<(PRODUCT_DIR)/audio.node' ],
        'destination': '<(module_root_dir)/<(target_arch)'
      }
    ]
  }
  ]
}
