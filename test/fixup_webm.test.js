const addon = require(".");
const fs = require("fs");

describe("WebM File Fixup", () => {
  describe("Synchronous API", () => {
    test("should successfully fix WebM files with various paths", () => {
      // Test with regular path
      let result = addon.fixup_webm(`${__dirname}/test.webm`, "./a.webm");
      fs.unlinkSync("a.webm");
      expect(result).toBe(0);

      // Test with non-ASCII characters in path
      result = addon.fixup_webm(`${__dirname}/中文/test.webm`, "./a.webm");
      fs.unlinkSync("a.webm");
      expect(result).toBe(0);

      // Test with non-ASCII characters in both input and output paths
      result = addon.fixup_webm(
        `${__dirname}/中文/test.webm`,
        `${__dirname}/中文/a.webm`
      );
      fs.unlinkSync(`${__dirname}/中文/a.webm`);
      expect(result).toBe(0);
    });

    test("should return error code when input file does not exist", () => {
      const result = addon.fixup_webm(
        `${__dirname}/notexists.webm`,
        "./a.webm"
      );
      expect(result).toBe(1);
    });
  });

  describe("Asynchronous API", () => {
    test("should successfully fix WebM file asynchronously", async () => {
      const result = await addon.fixup_webm_async(
        `${__dirname}/test.webm`,
        "./a.webm"
      );
      fs.unlinkSync("a.webm");
      expect(result).toBe(0);
    });

    test("should return error code when input file does not exist (async)", async () => {
      const result = await addon.fixup_webm_async(
        `${__dirname}/notexists.webm`,
        "./a.webm"
      );
      expect(result).toBe(1);
    });
  });
});
