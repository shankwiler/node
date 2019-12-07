EJSON = {};
EJSON.clone = function (v) {
  if (typeof v !== "object" || v === null)
    return v;

  if (v.length !== undefined) {

    if (Array.isArray(v) || (v.callee && Object.prototype.toString.call(v) == '[object Arguments]')) {
      var ret = new Array(v.length);
      for (var i = 0; i < v.length; ++i)
        ret[i] = EJSON.clone(v[i]);
      return ret;
    }

    if (v instanceof Uint8Array) {
      var ret = EJSON.newBinary(v.length);
      for (var i = 0; i < v.length; ++i)
        ret[i] = v[i];
      return ret;
    }

  }

  if (v.getDate && v instanceof Date)
    return new Date(v.getTime());

  if (v.compile && v instanceof RegExp)
    return v;

  if (v.clone && typeof v.clone === 'function')
    return v.clone();

  if (v.toJSONValue && EJSON._isCustomType(v))
    return EJSON.fromJSONValue(EJSON.clone(EJSON.toJSONValue(v)), true);

  var ret = {};
  for (var key in v)
    ret[key] = EJSON.clone(v[key]);

  return ret;
};

const { performance } = require('perf_hooks');
const arr = (() => {
    const a = [{}, {}, {}, {}, {}, {},{}, {}, {}, {}, {}, {},{}, {}, {}, {}, {}, {},{}, {}, {}, {}, {}, {},{}, {}, {}, {}, {}, {},{}, {}, {}, {}, {}, {},{}, {}, {}, {}, {}, {},{}, {}, {}, {}, {}, {},{}, {}, {}, {}, {}, {},{}, {}, {}, {}, {}, {},];
    const b = [];
    for (let i = 0; i < 10; ++i) {
        b.push(a);
    }
    const c = [];
    for (let i = 0; i < 10; ++i) {
        c.push(JSON.parse(JSON.stringify(b)));
    }
    return c;
})();

var test;
var test2;
var test3;
const before = performance.now();
for (let i = 0; i < 10000; ++i) {
    test = EJSON.clone(arr);
}
const after = performance.now();

const before2 = performance.now();
for (let i = 0; i < 10000; ++i) {
    test2 = JSON.stringify2(arr);
}
const after2 = performance.now();

const before3 = performance.now();
for (let i = 0; i < 10000; ++i) {
    test3 = JSON.parse(JSON.stringify((arr)));
}
const after3 = performance.now();


console.log(after - before, after2 - before2, after3 - before3);