const { Recorder } = require('../index.js');

// Example usage of the new AddWebm method
async function testAddWebm() {
  try {
    // Create a recorder instance
    const recorder = new Recorder('output.mp4', {
      fps: 30,
      width: 1920,
      height: 1080,
      bitrate: 5000000
    });

    // Example: Add a WebM blob from MediaRecorder
    // This would typically come from a MediaRecorder's ondataavailable event
    const webmBlob = new Blob([/* WebM data */], { type: 'video/webm' });

    // Convert Blob to Buffer (in a real browser environment)
    // const arrayBuffer = await webmBlob.arrayBuffer();
    // const buffer = Buffer.from(arrayBuffer);

    // For testing, we'll use a placeholder buffer
    const buffer = Buffer.alloc(1024); // Placeholder

    // Add the WebM blob to the recording
    const success = recorder.AddWebm(buffer);

    if (success) {
      console.log('Successfully added WebM blob to recording');
    } else {
      console.log('Failed to add WebM blob to recording');
    }

    // Close the recorder
    recorder.Close();

  } catch (error) {
    console.error('Error testing AddWebm:', error);
  }
}

// Example of how to use with MediaRecorder in a browser environment
function exampleWithMediaRecorder() {
  /*
  // In a browser environment:

  // 1. Start MediaRecorder
  const stream = await navigator.mediaDevices.getDisplayMedia({ video: true });
  const mediaRecorder = new MediaRecorder(stream, {
    mimeType: 'video/webm;codecs=vp9'
  });

  // 2. Collect WebM chunks
  const chunks = [];
  mediaRecorder.ondataavailable = async (event) => {
    if (event.data.size > 0) {
      chunks.push(event.data);
    }
  };

  // 3. When recording stops, process the WebM blob
  mediaRecorder.onstop = async () => {
    const webmBlob = new Blob(chunks, { type: 'video/webm' });
    const arrayBuffer = await webmBlob.arrayBuffer();
    const buffer = Buffer.from(arrayBuffer);

    // Add to recorder
    recorder.AddWebm(buffer);
  };

  // 4. Start recording
  mediaRecorder.start(1000); // Collect data every second
  */
}

console.log('AddWebm method test file created');
console.log('This method allows you to add VP8/VP9 encoded WebM blobs to your recording');
console.log('Usage: recorder.AddWebm(webmBuffer)');