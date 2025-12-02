const addon = require("..");

const args = process.argv.splice(2);
r = addon.get_meta_tags(args[0], "1234" );
console.log(r);
