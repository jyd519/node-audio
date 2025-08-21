const addon = require('.');

async function main() {
  const args = process.argv.splice(2);
  console.log(args)
  let r = addon.fixup_webm(args[0], args[1], {
      monitor: JSON.stringify({ a: 111, b: "xxxx" }),
  });

  console.log(r);

  var tags = await addon.get_meta_tags(args[1]);
  console.log("tags:", tags);
}

main();
