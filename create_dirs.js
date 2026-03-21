const fs = require('fs');
['src', 'include', 'shaders', 'libs/glad/include/glad', 'libs/glad/src'].forEach(d => fs.mkdirSync('c:/Users/2012m/Desktop/SIMULATOR/' + d, { recursive: true }));
console.log('done');
