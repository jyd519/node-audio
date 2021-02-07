{
  "variables": {
    "libwebm_root%": "",
  },
  "targets": [
  {
    "target_name": "audio",
    "sources": [ "audio-napi.cc", "webvtt/vttreader.cc", "webvtt/webvttparser.cc", "webm_muxer.cc", "sample_muxer_metadata.cc" ],
    "conditions": [
      ['OS=="mac"',
      {
        'defines': [
          '__MACOSX_CORE__'
        ],
        'include_dirs': ['<(libwebm_root)'],
        'link_settings': {
          'libraries': [
            '-framework', 'CoreAudio',
            '-lwebm', 
            '-L<(libwebm_root)/lib',
          ]
        },
        'xcode_settings': {
          'GCC_ENABLE_CPP_EXCEPTIONS': 'YES'
        }
      }
      ],
      ['OS=="win"',
      {
        'defines': [
        ],
        'include_dirs': ['<(libwebm_root)', '.'],
        'link_settings': {
          'libraries': [
            '<(libwebm_root)\\lib\\libwebm.lib',
          ]
       },
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
