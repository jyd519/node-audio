#include "fixwebm.h"

#include "avcpp/av.h"
#include "avcpp/packet.h"
#include "avcpp/formatcontext.h"

#ifndef NO_ENCRYPTION
#include "enc_writer.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif
#include <libavutil/log.h>
#ifdef __cplusplus
}
#endif

// remux the webm file and attempt to recover corrupted WebM files
bool remuxWebmFile(const std::string &inputPath, const std::string &outputPath,
                   const std::map<std::string, std::string> &metadata) {
  try {
    // Initialize FFmpeg
    //av::init();

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

    // Map for tracking stream indices
    std::map<int, int> streamMapping;

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

      // Copy codec parameters
      auto codecParams = inStream.codecParameters();
      outStream.setCodecParameters(codecParams, streamEc);
      if (streamEc) {
        av_log(nullptr, AV_LOG_ERROR, "Error setting codec parameters: %s\n",
               streamEc.message().c_str());
        return false;
      }
    }

    // Write output header
    av::Dictionary headerOptions;
    if (auto pos = outputPath.find_last_of("."); pos != std::string::npos) {
      if (outputPath.substr(pos) == ".mp4") {
          headerOptions.set("movflags", "+faststart+use_metadata_tags");
      }
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

      // Adjust packet stream index to output context
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
