#include "fixwebm.h"

#include "avcpp/packet.h"
#include "avcpp/formatcontext.h"
#include "avcpp/codeccontext.h"
#include "avcpp/codec.h"
#include "avcpp/frame.h"
#include "avcpp/audioresampler.h"
#include <cstring>

#ifndef NO_ENCRYPTION
#include "enc_writer.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif
#include <libavutil/log.h>
#include <libavcodec/avcodec.h>
#include <libavutil/fifo.h>
#include <libavutil/samplefmt.h>
#ifdef __cplusplus
}
#endif

// remux the webm file and attempt to recover corrupted WebM files
bool remuxWebmFile(const std::string &inputPath, const std::string &outputPath,
                   const std::map<std::string, std::string> &metadata) {
  try {
    // Initialize FFmpeg
    // av::init();

    // Set up input format context with error recovery flags
    av::FormatContext inputContext;
    av::Dictionary inputOptions;

    // Add genpts flag to generate presentation timestamps if missing
    inputOptions.set("fflags", "+genpts");

    // Add error recovery options for corrupted files
    inputOptions.set("err_detect", "ignore_err");

    // Open input file with recovery options
    std::error_code ec;
    inputContext.openInput(inputPath, inputOptions, ec);
    if (ec) {
      av_log(nullptr, AV_LOG_ERROR, "Error opening input file: %s\n", ec.message().c_str());
      return false;
    }

    // Find stream information
    try {
      inputContext.findStreamInfo();
    } catch (const std::exception &e) {
      av_log(nullptr, AV_LOG_ERROR, "Error finding stream info: %s\n", e.what());
      return false;
    }

    // Create output format context
    av::FormatContext outputContext;
#ifndef NO_ENCRYPTION
    std::unique_ptr<EncryptWriter> owriter;
    std::string password;
    if (auto p = metadata.find("password"); p != metadata.end()) {
      password = p->second;
    }
#endif
    try {
#ifndef NO_ENCRYPTION
      if (!password.empty()) {
        owriter.reset(new EncryptWriter(outputPath, password));
        outputContext.setFormat(av::OutputFormat("", outputPath));
        outputContext.openOutput(owriter.get());
      } else {
        outputContext.openOutput(outputPath);
      }
#else
      outputContext.openOutput(outputPath);
#endif
    } catch (const std::exception &e) {
      av_log(nullptr, AV_LOG_ERROR, "Error creating output file: %s\n", e.what());
      return false;
    }

    // Check if output is MP4
    bool isMp4 = false;
    if (auto pos = outputPath.find_last_of("."); pos != std::string::npos) {
      isMp4 = (outputPath.substr(pos) == ".mp4");
    }

    // Map for tracking stream indices
    std::map<int, int> streamMapping;
    // Map for audio transcoding: input stream index -> decoder/encoder contexts
    std::map<int, std::pair<av::AudioDecoderContext, av::AudioEncoderContext>> audioTranscoders;
    // Map for audio resamplers if needed
    std::map<int, av::AudioResampler> audioResamplers;
    // Map for audio sample FIFOs: input stream index -> FIFO
    std::map<int, AVFifo*> audioFifos;

    // Set up output streams by copying codec parameters from input
    for (size_t i = 0; i < inputContext.streamsCount(); ++i) {
      auto inStream = inputContext.stream(i);
      auto mediaType = inStream.mediaType();

      // Only process audio and video streams
      if (mediaType != AVMEDIA_TYPE_AUDIO && mediaType != AVMEDIA_TYPE_VIDEO) {
        streamMapping[(int)i] = -1;
        continue;
      }

      streamMapping[i] = (int)outputContext.streamsCount();

      // Create new stream in output
      std::error_code streamEc;
      auto outStream = outputContext.addStream(streamEc);
      if (streamEc) {
        av_log(nullptr, AV_LOG_ERROR, "Error creating output stream: %s\n",
               streamEc.message().c_str());
        return false;
      }

      // For MP4 files, check if audio needs transcoding
      if (isMp4 && mediaType == AVMEDIA_TYPE_AUDIO) {
        auto codecParams = inStream.codecParameters();
        auto codecId = codecParams.codecId();

        // Check if audio codec needs transcoding (non-AAC codecs like PCM_ALAW, PCM_MULAW)
        bool needsTranscoding = false;
        if (codecId != AV_CODEC_ID_AAC) {
          // Check for PCM codecs that need transcoding
          if (codecId == AV_CODEC_ID_PCM_ALAW || codecId == AV_CODEC_ID_PCM_MULAW ||
              codecId == AV_CODEC_ID_PCM_S16LE || codecId == AV_CODEC_ID_PCM_S16BE ||
              codecId == AV_CODEC_ID_PCM_U8 || codecId == AV_CODEC_ID_PCM_S8) {
            needsTranscoding = true;
            av_log(nullptr, AV_LOG_INFO, "Audio codec %d needs transcoding to AAC for MP4\n", codecId);
          }
        }

        if (needsTranscoding) {
          // Create decoder for input stream
          av::AudioDecoderContext decoder(inStream);
          std::error_code decodeEc;
          decoder.open(decodeEc);
          if (decodeEc) {
            av_log(nullptr, AV_LOG_ERROR, "Error opening audio decoder: %s\n", decodeEc.message().c_str());
            return false;
          }

          // Create AAC encoder
          auto aacCodec = av::findEncodingCodec(AV_CODEC_ID_AAC);
          if (aacCodec.isNull()) {
            av_log(nullptr, AV_LOG_ERROR, "AAC encoder not found\n");
            return false;
          }

          av::AudioEncoderContext encoder(aacCodec);

          // Set encoder parameters based on decoder
          encoder.setSampleRate(44100);
          encoder.setChannels(2);
          encoder.setChannelLayout(AV_CH_LAYOUT_STEREO);
          encoder.setBitRate(64000); // 64 kbps for AAC

          // Set sample format (AAC typically uses fltp)
          auto supportedFormats = aacCodec.supportedSampleFormats();
          if (!supportedFormats.empty()) {
            encoder.setSampleFormat(supportedFormats[0]);
          }

          // Open encoder
          std::error_code encodeEc;
          encoder.open(encodeEc);
          if (encodeEc) {
            av_log(nullptr, AV_LOG_ERROR, "Error opening AAC encoder: %s\n", encodeEc.message().c_str());
            return false;
          }

          // Create resampler if sample format or rate differs
          if (decoder.sampleFormat() != encoder.sampleFormat() ||
              decoder.sampleRate() != encoder.sampleRate() ||
              decoder.channelLayout() != encoder.channelLayout()) {
            auto decLayout = AV_CH_LAYOUT_MONO;
            if (decoder.channelLayout() > 0) {
              decLayout = decoder.channelLayout();
            }
            av::AudioResampler resampler(
              encoder.channelLayout(), encoder.sampleRate(), encoder.sampleFormat(),
              decLayout, decoder.sampleRate(), decoder.sampleFormat(),
              streamEc);
            if (streamEc || !resampler.isValid()) {
              av_log(nullptr, AV_LOG_WARNING, "Warning: Could not create audio resampler: %s\n",
                     streamEc ? streamEc.message().c_str() : "Resampler not valid");
            } else {
              // bugfix: AudioResampler do not implement the move constructor properly!
              audioResamplers[(int)i] = std::move(resampler);
              audioResamplers[(int)i].reset(resampler.raw());
              resampler.reset(nullptr);
            }
          }

          // Set codec parameters from encoder to output stream
          // Copy parameters from encoder context to stream
          std::error_code paramEc;
          outStream.codecParameters().copyFrom(encoder, paramEc);
          if (paramEc) {
            av_log(nullptr, AV_LOG_ERROR, "Error setting encoder codec parameters: %s\n",
                   paramEc.message().c_str());
            return false;
          }

          // Store transcoder contexts
          audioTranscoders[(int)i] = std::make_pair(std::move(decoder), std::move(encoder));

          // Create FIFO for buffering samples
          AVFifo* fifo = av_fifo_alloc2(32, sizeof(av::AudioSamples*), AV_FIFO_FLAG_AUTO_GROW);
          if (!fifo) {
            av_log(nullptr, AV_LOG_ERROR, "Error creating audio FIFO\n");
            return false;
          }
          audioFifos[(int)i] = fifo;
        } else {
          // Copy codec parameters directly
          auto codecParams = inStream.codecParameters();
          outStream.setCodecParameters(codecParams, streamEc);
          if (streamEc) {
            av_log(nullptr, AV_LOG_ERROR, "Error setting codec parameters: %s\n",
                   streamEc.message().c_str());
            return false;
          }
        }
      } else {
        // Copy codec parameters directly for video or non-MP4
      auto codecParams = inStream.codecParameters();
      outStream.setCodecParameters(codecParams, streamEc);
      if (streamEc) {
        av_log(nullptr, AV_LOG_ERROR, "Error setting codec parameters: %s\n",
               streamEc.message().c_str());
        return false;
        }
      }
    }

    // Write output header
    av::Dictionary headerOptions;
    if (isMp4) {
      headerOptions.set("movflags", "+faststart+use_metadata_tags");
    }

    // Note: We don't need to set AVFMT_GLOBALHEADER explicitly as it's handled internally
    std::error_code headerEc;
    // Apply all metadata from the map
    for (const auto &[key, value] : metadata) {
      if (key == "password") {
        continue;
      }
      av_dict_set(&outputContext.raw()->metadata, key.c_str(), value.c_str(), 0);
    }

    // Set default metadata if none provided
    if (metadata.empty()) {
      av_dict_set(&outputContext.raw()->metadata, "comment", "ata", 0);
    }
    outputContext.writeHeader(headerOptions, headerEc);
    if (headerEc) {
      av_log(nullptr, AV_LOG_ERROR, "Error writing header: %s\n", headerEc.message().c_str());
      return false;
    }

    // Copy packets from input to output
    bool success = true;

    while (true) {
      std::error_code packetEc;
      av::Packet packet = inputContext.readPacket(packetEc);

      if (packetEc) {
        // If we hit an error but have processed some packets, try to continue
        if (packetEc.value() == AVERROR_EOF) {
          // Normal EOF
          break;
        }

        av_log(nullptr, AV_LOG_WARNING, "Warning: Error reading packet: %s\n",
               packetEc.message().c_str());
        // Try to continue despite errors
        continue;
      }

      if (packet.isNull()) {
        av_log(nullptr, AV_LOG_WARNING, "Warning: Empty packet.\n");
        break;
      }

      // Skip packets from streams we're not processing
      int streamIndex = packet.streamIndex();
      if (streamMapping.find(streamIndex) == streamMapping.end() ||
          streamMapping[streamIndex] < 0) {
        continue;
      }

      // Check if this stream needs transcoding
      if (audioTranscoders.find(streamIndex) != audioTranscoders.end()) {
        // Transcode audio: decode -> resample (if needed) -> encode
        auto &transcoder = audioTranscoders[streamIndex];
        auto &decoder = transcoder.first;
        auto &encoder = transcoder.second;

        // Decode packet
        std::error_code decodeEc;
        av::AudioSamples samples = decoder.decode(packet, decodeEc);
        if (decodeEc && decodeEc.value() != AVERROR(EAGAIN)) {
          av_log(nullptr, AV_LOG_WARNING, "Warning: Error decoding audio packet: %s\n",
                 decodeEc.message().c_str());
          continue;
        }

        if (samples.isComplete() && !samples.isNull()) {
          // Resample if needed
          av::AudioSamples samplesToQueue = samples;
          if (audioResamplers.find(streamIndex) != audioResamplers.end()) {
            auto &resampler = audioResamplers[streamIndex];
            if (!resampler.isValid()) {
              av_log(nullptr, AV_LOG_WARNING, "Warning: Resampler is not valid, skipping resampling\n");
            } else {
              std::error_code pushEc;
              if (samples.channelsLayout() == 0) {
                av_channel_layout_uninit(&samples.raw()->ch_layout);
                av_channel_layout_from_mask(&samples.raw()->ch_layout, AV_CH_LAYOUT_MONO);
              }
              resampler.push(samples, pushEc);
              if (pushEc) {
                av_log(nullptr, AV_LOG_WARNING, "Warning: Error pushing samples to resampler: %s\n",
                       pushEc.message().c_str());
                continue;
              }

              // Pop all available resampled samples
              while (true) {
                std::error_code popEc;
                av::AudioSamples resampledSamples = resampler.pop(0, popEc);
                if (popEc || resampledSamples.isNull() || resampledSamples.samplesCount() == 0) {
                  break;
                }

                // Add resampled samples to FIFO
                av::AudioSamples* samplesPtr = new av::AudioSamples(std::move(resampledSamples));
                av_fifo_write(audioFifos[streamIndex], &samplesPtr, 1);
              }
              continue; // Skip direct encoding path
            }
          }

          // No resampling needed, add samples directly to FIFO
          av::AudioSamples* samplesPtr = new av::AudioSamples(std::move(samplesToQueue));
          av_fifo_write(audioFifos[streamIndex], &samplesPtr, 1);
        }

        // Process samples from FIFO and encode
        int encoderFrameSize = encoder.frameSize();
        if (encoderFrameSize <= 0) {
          encoderFrameSize = 1024; // Default AAC frame size
        }

        // Try to get enough samples from FIFO to encode
        while (true) {
          // Count total samples in FIFO
          int totalSamples = 0;
          av::AudioSamples* peekPtr = nullptr;
          for (size_t i = 0; i < av_fifo_can_read(audioFifos[streamIndex]); i++) {
            if (av_fifo_peek(audioFifos[streamIndex], &peekPtr, 1, i) >= 0 && peekPtr) {
              totalSamples += peekPtr->samplesCount();
            }
          }

          // If we don't have enough samples, wait for more
          if (totalSamples < encoderFrameSize) {
            break;
          }

          // Create frame with exact frame size
          av::AudioSamples frameToEncode(
            encoder.sampleFormat(),
            encoderFrameSize,
            encoder.channelLayout(),
            encoder.sampleRate()
          );

          // Copy samples from FIFO to frame
          int samplesCopied = 0;
          while (samplesCopied < encoderFrameSize) {
            av::AudioSamples* fifoSamples = nullptr;
            if (av_fifo_peek(audioFifos[streamIndex], &fifoSamples, 1, 0) < 0 || !fifoSamples) {
              break;
            }

            int samplesNeeded = encoderFrameSize - samplesCopied;
            int samplesToCopy = (fifoSamples->samplesCount() > samplesNeeded) ? samplesNeeded : fifoSamples->samplesCount();

            // Copy audio data
            int channels = encoder.channels();
            int bytesPerSample = av_get_bytes_per_sample(static_cast<AVSampleFormat>(encoder.sampleFormat()));
            int isPlanar = av_sample_fmt_is_planar(static_cast<AVSampleFormat>(encoder.sampleFormat()));

            if (isPlanar) {
              for (int ch = 0; ch < channels; ch++) {
                memcpy(frameToEncode.raw()->data[ch] + samplesCopied * bytesPerSample,
                       fifoSamples->raw()->data[ch],
                       samplesToCopy * bytesPerSample);
              }
            } else {
              int copySize = samplesToCopy * channels * bytesPerSample;
              memcpy(frameToEncode.raw()->data[0] + samplesCopied * channels * bytesPerSample,
                     fifoSamples->raw()->data[0],
                     copySize);
            }

            samplesCopied += samplesToCopy;

            // Remove samples from FIFO
            if (samplesToCopy >= fifoSamples->samplesCount()) {
              // Consumed entire frame, remove it
              av_fifo_drain2(audioFifos[streamIndex], 1);
              delete fifoSamples;
            } else {
              // Partially consumed, update the frame
              int remaining = fifoSamples->samplesCount() - samplesToCopy;
              if (isPlanar) {
                for (int ch = 0; ch < channels; ch++) {
                  memmove(fifoSamples->raw()->data[ch],
                          fifoSamples->raw()->data[ch] + samplesToCopy * bytesPerSample,
                          remaining * bytesPerSample);
                }
              } else {
                memmove(fifoSamples->raw()->data[0],
                        fifoSamples->raw()->data[0] + samplesToCopy * channels * bytesPerSample,
                        remaining * channels * bytesPerSample);
              }
              fifoSamples->raw()->nb_samples = remaining;
            }
          }

          if (samplesCopied < encoderFrameSize) {
            break; // Not enough samples, wait for more
          }

          // Encode the frame
          std::error_code encodeEc;
          av::Packet encodedPacket = encoder.encode(frameToEncode, encodeEc);
          if (encodeEc && encodeEc.value() != AVERROR(EAGAIN)) {
            av_log(nullptr, AV_LOG_WARNING, "Warning: Error encoding audio: %s\n",
                   encodeEc.message().c_str());
            break;
          }

          if (encodedPacket.isComplete() && !encodedPacket.isNull()) {
            encodedPacket.setStreamIndex(streamMapping[streamIndex]);
            std::error_code writeEc;
            outputContext.writePacket(encodedPacket, writeEc);
            if (writeEc) {
              av_log(nullptr, AV_LOG_WARNING, "Warning: Error writing encoded packet: %s\n",
                     writeEc.message().c_str());
              success = false;
            }
          }

          // Try to get more encoded packets (encoder may buffer)
          while (true) {
            std::error_code flushEc;
            av::Packet flushPacket = encoder.encode(flushEc);
            if (flushEc && flushEc.value() != AVERROR(EAGAIN)) {
              break;
            }
            if (flushPacket.isNull() || !flushPacket.isComplete()) {
              break;
            }
            flushPacket.setStreamIndex(streamMapping[streamIndex]);
            std::error_code writeEc;
            outputContext.writePacket(flushPacket, writeEc);
            if (writeEc) {
              av_log(nullptr, AV_LOG_WARNING, "Warning: Error writing flushed packet: %s\n",
                     writeEc.message().c_str());
            }
          }
        }
      } else {
        // Direct copy for video or non-transcoded audio
      packet.setStreamIndex(streamMapping[streamIndex]);

      // Write packet to output
      std::error_code writeEc;
      outputContext.writePacket(packet, writeEc);
      if (writeEc) {
        av_log(nullptr, AV_LOG_WARNING, "Warning: Error writing packet: %s\n",
               writeEc.message().c_str());
        // Continue processing despite errors
        success = false;
      }
      }
    }

    // Flush all audio encoders
    for (auto &[streamIdx, transcoder] : audioTranscoders) {
      auto &decoder = transcoder.first;
      auto &encoder = transcoder.second;

      // If resampler exists and is valid, flush it first and add samples to FIFO
      if (audioResamplers.find(streamIdx) != audioResamplers.end()) {
        auto &resampler = audioResamplers[streamIdx];
        if (resampler.isValid()) {
          // Push null samples to flush resampler
          std::error_code pushEc;
          resampler.push(av::AudioSamples(), pushEc);

          // Pop all remaining resampled samples and add to FIFO
          while (true) {
            std::error_code popEc;
            av::AudioSamples resampledSamples = resampler.pop(0, popEc);
            if (popEc || resampledSamples.isNull() || resampledSamples.samplesCount() == 0) {
              break;
            }

            av::AudioSamples* samplesPtr = new av::AudioSamples(std::move(resampledSamples));
            av_fifo_write(audioFifos[streamIdx], &samplesPtr, 1);
          }
        }
      }

      // Process remaining samples in FIFO
      int encoderFrameSize = encoder.frameSize();
      if (encoderFrameSize <= 0) {
        encoderFrameSize = 1024;
      }

      while (av_fifo_can_read(audioFifos[streamIdx]) > 0) {
        // Count total samples in FIFO
        int totalSamples = 0;
        av::AudioSamples* peekPtr = nullptr;
        for (size_t i = 0; i < av_fifo_can_read(audioFifos[streamIdx]); i++) {
          if (av_fifo_peek(audioFifos[streamIdx], &peekPtr, 1, i) >= 0 && peekPtr) {
            totalSamples += peekPtr->samplesCount();
          }
        }

        // If we have enough samples, encode a frame
        if (totalSamples >= encoderFrameSize) {
          av::AudioSamples frameToEncode(
            encoder.sampleFormat(),
            encoderFrameSize,
            encoder.channelLayout(),
            encoder.sampleRate()
          );

          int samplesCopied = 0;
          while (samplesCopied < encoderFrameSize) {
            av::AudioSamples* fifoSamples = nullptr;
            if (av_fifo_peek(audioFifos[streamIdx], &fifoSamples, 1, 0) < 0 || !fifoSamples) {
              break;
            }

            int samplesNeeded = encoderFrameSize - samplesCopied;
            int samplesToCopy = (fifoSamples->samplesCount() > samplesNeeded) ? samplesNeeded : fifoSamples->samplesCount();

            int channels = encoder.channels();
            int bytesPerSample = av_get_bytes_per_sample(static_cast<AVSampleFormat>(encoder.sampleFormat()));
            int isPlanar = av_sample_fmt_is_planar(static_cast<AVSampleFormat>(encoder.sampleFormat()));

            if (isPlanar) {
              for (int ch = 0; ch < channels; ch++) {
                memcpy(frameToEncode.raw()->data[ch] + samplesCopied * bytesPerSample,
                       fifoSamples->raw()->data[ch],
                       samplesToCopy * bytesPerSample);
              }
            } else {
              int copySize = samplesToCopy * channels * bytesPerSample;
              memcpy(frameToEncode.raw()->data[0] + samplesCopied * channels * bytesPerSample,
                     fifoSamples->raw()->data[0],
                     copySize);
            }

            samplesCopied += samplesToCopy;

            if (samplesToCopy >= fifoSamples->samplesCount()) {
              av_fifo_drain2(audioFifos[streamIdx], 1);
              delete fifoSamples;
            } else {
              int remaining = fifoSamples->samplesCount() - samplesToCopy;
              if (isPlanar) {
                for (int ch = 0; ch < channels; ch++) {
                  memmove(fifoSamples->raw()->data[ch],
                          fifoSamples->raw()->data[ch] + samplesToCopy * bytesPerSample,
                          remaining * bytesPerSample);
                }
              } else {
                memmove(fifoSamples->raw()->data[0],
                        fifoSamples->raw()->data[0] + samplesToCopy * channels * bytesPerSample,
                        remaining * channels * bytesPerSample);
              }
              fifoSamples->raw()->nb_samples = remaining;
            }
          }

          if (samplesCopied == encoderFrameSize) {
            std::error_code encodeEc;
            av::Packet encodedPacket = encoder.encode(frameToEncode, encodeEc);
            if (!encodeEc || encodeEc.value() == AVERROR(EAGAIN)) {
              if (encodedPacket.isComplete() && !encodedPacket.isNull()) {
                encodedPacket.setStreamIndex(streamMapping[streamIdx]);
                std::error_code writeEc;
                outputContext.writePacket(encodedPacket, writeEc);
              }
            }
          }
        } else {
          // Not enough samples, pad with silence and encode
          if (totalSamples > 0) {
            av::AudioSamples frameToEncode(
              encoder.sampleFormat(),
              encoderFrameSize,
              encoder.channelLayout(),
              encoder.sampleRate()
            );

            // Fill with silence first
            av_samples_set_silence(frameToEncode.raw()->data, 0, encoderFrameSize,
                                    encoder.channels(), static_cast<AVSampleFormat>(encoder.sampleFormat()));

            // Copy available samples
            int samplesCopied = 0;
            while (samplesCopied < totalSamples) {
              av::AudioSamples* fifoSamples = nullptr;
              if (av_fifo_peek(audioFifos[streamIdx], &fifoSamples, 1, 0) < 0 || !fifoSamples) {
                break;
              }

              int samplesToCopy = fifoSamples->samplesCount();
              int channels = encoder.channels();
              int bytesPerSample = av_get_bytes_per_sample(static_cast<AVSampleFormat>(encoder.sampleFormat()));
              int isPlanar = av_sample_fmt_is_planar(static_cast<AVSampleFormat>(encoder.sampleFormat()));

              if (isPlanar) {
                for (int ch = 0; ch < channels; ch++) {
                  memcpy(frameToEncode.raw()->data[ch] + samplesCopied * bytesPerSample,
                         fifoSamples->raw()->data[ch],
                         samplesToCopy * bytesPerSample);
                }
              } else {
                int copySize = samplesToCopy * channels * bytesPerSample;
                memcpy(frameToEncode.raw()->data[0] + samplesCopied * channels * bytesPerSample,
                       fifoSamples->raw()->data[0],
                       copySize);
              }

              samplesCopied += samplesToCopy;
              av_fifo_drain2(audioFifos[streamIdx], 1);
              delete fifoSamples;
            }

            std::error_code encodeEc;
            av::Packet encodedPacket = encoder.encode(frameToEncode, encodeEc);
            if (!encodeEc || encodeEc.value() == AVERROR(EAGAIN)) {
              if (encodedPacket.isComplete() && !encodedPacket.isNull()) {
                encodedPacket.setStreamIndex(streamMapping[streamIdx]);
                std::error_code writeEc;
                outputContext.writePacket(encodedPacket, writeEc);
              }
            }
          }
          break;
        }
      }

      // Flush encoder
      while (true) {
        std::error_code flushEc;
        av::Packet flushPacket = encoder.encode(flushEc);
        if (flushEc && flushEc.value() != AVERROR(EAGAIN) && flushEc.value() != AVERROR_EOF) {
          break;
        }
        if (flushPacket.isNull() || !flushPacket.isComplete()) {
          break;
        }
        flushPacket.setStreamIndex(streamMapping[streamIdx]);
        std::error_code writeEc;
        outputContext.writePacket(flushPacket, writeEc);
        if (writeEc) {
          av_log(nullptr, AV_LOG_WARNING, "Warning: Error writing final flushed packet: %s\n",
                 writeEc.message().c_str());
        }
      }

      // Clean up FIFO
      av::AudioSamples* fifoSamples = nullptr;
      while (av_fifo_read(audioFifos[streamIdx], &fifoSamples, 1) >= 0) {
        if (fifoSamples) {
          delete fifoSamples;
        }
      }
      av_fifo_freep2(&audioFifos[streamIdx]);
    }

    // Write trailer
    std::error_code trailerEc;
    outputContext.writeTrailer(trailerEc);
    if (trailerEc) {
      av_log(nullptr, AV_LOG_ERROR, "Error writing trailer: %s\n", trailerEc.message().c_str());
      success = false;
    }

    // Close contexts
    inputContext.close();
    outputContext.close();

    return success;
  } catch (const std::exception &e) {
    av_log(nullptr, AV_LOG_ERROR, "Unexpected error in fixWebmDuration: %s\n", e.what());
    return false;
  }
}
