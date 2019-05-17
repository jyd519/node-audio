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
  }
  ]
}
