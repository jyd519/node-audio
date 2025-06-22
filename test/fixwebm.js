const addon = require(".");

async function main() {
  await addon.fixup_webm(
    "jk.webm",
    "jk1.webm",
  );

  await addon.fixup_webm_async(
    "jk.webm",
    "jk2.webm",
    {"title": "XXXX",  comment: "ATA 2025"}
  );
}

main().catch(console.log);
