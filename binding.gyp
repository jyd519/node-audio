{
  "variables": {
    "libwebm_include%": "/opt/works/libwebm",
    "libwebm_lib_path%": "/opt/works/libwebm",
    "avhelp_lib_path%": "/opt/works/libwebm",
    "targetarch%": "amd64",
  },
  "targets": [
  {
    "target_name": "audio",
    "sources": [ "src/audio-napi.cc", "src/napi_help.cc", "src/av_help.cc", "webvtt/vttreader.cc", 
                 "webvtt/webvttparser.cc", "src/webm_muxer.cc", "src/sample_muxer_metadata.cc" ],
    "conditions": [
      ['OS=="mac"',
      {
        'defines': [
          '__MACOSX_CORE__'
        ],
        'include_dirs': ['<(libwebm_include)', '.'],
        'link_settings': {
          'libraries': [
            '-framework', 'CoreAudio',
            '-lwebm', 
            '-L<(libwebm_lib_path)/lib',
          ]
        },
        'xcode_settings': {
          'GCC_ENABLE_CPP_EXCEPTIONS': 'YES'
        }
      }
      ],
      ['OS=="win"',
      {
        'win_delay_load_hook': 'false',
        'defines': [ 'HOST_BINARY=\"<(node_host_binary)<(EXECUTABLE_SUFFIX)\"', ],
        'include_dirs': ['<(libwebm_include)', '.'],
        'link_settings': {
          'libraries': [
            '<(libwebm_lib_path)\\webm.lib',
            '<(module_root_dir)\\<(target_arch)\\avhelp.lib',
          ]
         },
         'sources': [
           'src/win_delay_load_hook.cc',
         ],
         'msvs_settings': {
           'VCLinkerTool': {
             'DelayLoadDLLs': [ '<(node_host_binary)<(EXECUTABLE_SUFFIX)', 'avhelp.dll' ],
             # Don't print a linker warning when no imports from either .exe
             # are used.
             'AdditionalOptions': [ '/ignore:4199' ],
           },
         },
      }
      ],
      ['OS=="linux"',
      {
        'include_dirs': ['<(libwebm_include)', '.'],
        'link_settings': {
          'ldflags': [
             "-Wl,-rpath,'$$ORIGIN'"
           ],
          'libraries': [
            '-lwebm', 
            '-lasound',
            '-lstdc++',
            '-L<(libwebm_lib_path)',
          ]
        }
      }
      ],
      ['OS=="linux" and targetarch=="arm64"',
      {
        'ldflags':  [
          '--sysroot',
          '/var/ata/electron/src/build/linux/debian_sid_arm64-sysroot/'
        ],
      }
      ],
    ]
  },
  {
    "target_name": "webm",
    "type": "shared_library",
    "sources": [ "webvtt/vttreader.cc", "webvtt/webvttparser.cc", "src/webm_muxer.cc", "src/sample_muxer_metadata.cc" ],
    "conditions": [
      ['OS=="mac"',
      {
        'defines': [
          '__MACOSX_CORE__'
        ],
        'include_dirs': ['<(libwebm_include)'],
        'link_settings': {
          'libraries': [
            '-lwebm', 
            '-L<(libwebm_lib_path)',
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
        'include_dirs': ['<(libwebm_include)'],
        'link_settings': {
          'libraries': [
            '<(libwebm_lib_path)\\webm.lib',
          ]
       },
      }
      ],
      ['OS=="linux"',
      {
        'include_dirs': ['<(libwebm_include)', '.'],
        'link_settings': {
          'libraries': [
            '-L<(libwebm_lib_path)',
            '-lwebm', 
            '-lstdc++',
          ]
        }
      }
      ],
    ]
  },
  {
    'target_name': 'action_after_build',
    'type': 'none',
    'dependencies': [ 'audio', 'webm' ],
    "conditions": [
       ['OS=="mac"', {
          'copies': [
            {
              'files': [ '<(PRODUCT_DIR)/audio.node', '<(PRODUCT_DIR)/webm.dylib'  ],
              'destination': '<(module_root_dir)/<(target_arch)'
            }
          ]
       }],
       ['OS=="win"', {
          'copies': [
              {
              'files': [ '<(PRODUCT_DIR)/audio.node', '<(PRODUCT_DIR)/webm.dll'  ],
              'destination': '<(module_root_dir)/<(target_arch)'
              }
          ]
       }],
    ]
  }
  ]
}
