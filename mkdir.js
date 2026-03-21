const fs = require('fs');
const dirs = [
  'c:/Users/2012m/Desktop/SIMULATOR/src',
  'c:/Users/2012m/Desktop/SIMULATOR/include',
  'c:/Users/2012m/Desktop/SIMULATOR/shaders',
  'c:/Users/2012m/Desktop/SIMULATOR/libs/glad/include/glad',
  'c:/Users/2012m/Desktop/SIMULATOR/libs/glad/src'
];

dirs.forEach(d => {
  fs.mkdirSync(d, { recursive: true });
  console.log(`Created: ${d}`);
});
console.log('done');
