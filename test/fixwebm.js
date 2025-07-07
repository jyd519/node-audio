const addon = require(".");

async function main() {
  await addon.fixup_webm("jk.webm", "jk1.webm");

  await addon.fixup_webm_async("jk.webm", "jk2.webm", {
    title: "XXXX",
    comment: "ATA 2025",
    ata: JSON.stringify({ a: 111, b: "xxxx" }),
    password: "1234",
  });

  var tags = await addon.get_meta_tags("jk1.webm");
  console.log("jk1:", tags);
  tags = await addon.get_meta_tags("jk2.webm", "1234");
  console.log("jk2:", tags);
}

main().catch(console.log);
