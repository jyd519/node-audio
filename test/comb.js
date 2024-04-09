const addon = require('.');

const args = process.argv.splice(2);

const argv = require('yargs')(args).default("out", "comb.mp4").alias("out", "o").parse();

console.log(argv);

function main() {
  if (argv._.length < 1) {
    console.log("need at least 2 files");
    return;
  }
  let r = addon.combine(argv.out, argv._);
  console.log(r);
}

main();
