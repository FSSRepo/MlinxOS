module.exports = function greeting(name) {
    return `Hello, ${name}! (require('./lib/greeting') from ${require('path').basename(__filename)})`;
};
