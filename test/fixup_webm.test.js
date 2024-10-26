const addon = require(".");

it('fixup_webm sync', () => {
  let r = addon.fixup_webm(`${__dirname}/test.webm`, './a.webm');
  expect(r).toEqual(0);
  r = addon.fixup_webm(`${__dirname}/中文/test.webm`, './a.webm');
  expect(r).toEqual(0);
  r = addon.fixup_webm(`${__dirname}/中文/test.webm`, `${__dirname}/中文/a.webm`);
  expect(r).toEqual(0);
});

it('fixup_webm sync failed', () => {
  let r = addon.fixup_webm(`${__dirname}/notexists.webm`, './a.webm');
  expect(r).toEqual(1);
});

it('fixup_webm_async', async () => {
  let r = await addon.fixup_webm_async(`${__dirname}/test.webm`, './a.webm');
  expect(r).toEqual(0);
});

it('fixup_webm_async failed', async () => {
  let r = await addon.fixup_webm_async(`${__dirname}/notexists.webm`, './a.webm');
  expect(r).toEqual(1);
});
