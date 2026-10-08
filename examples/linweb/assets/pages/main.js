print('Hello from main.js!');

const header = document.getElementsByClassName('blue-box')[0];
if (header) {
    header.addEventListener('click', () => {
        print('Header clicked!');
        header.style.display = 'none';
    });
}

const redBox = document.getElementsByClassName('red-box')[0];
if (redBox) {
    redBox.addEventListener('mouseenter', () => {
        print('Red box hovered!');
    });
    redBox.addEventListener('mouseleave', () => {
        print('Red box left!');
    });
}

const main = document.getElementsByClassName('main')[0];
if (main) {
    let t = 0;
    function animate() {
        t += 0.02;
        let r = Math.floor(127 + 127 * Math.sin(t));
        let g = Math.floor(127 + 127 * Math.sin(t + 2));
        let b = Math.floor(127 + 127 * Math.sin(t + 4));
        let color = '#' + r.toString(16).padStart(2, '0') + 
                          g.toString(16).padStart(2, '0') + 
                          b.toString(16).padStart(2, '0');
        main.style.backgroundColor = color;
        requestAnimationFrame(animate);
    }
    animate(); // Enable background animation
}

// Example of keydown event
document.addEventListener('keydown', (event) => {
    print('Key pressed: ' + event.key);
    
    const header = document.getElementById('header');
    if (header) {
        if (event.key === 'r') header.style.backgroundColor = '#ff0000';
        if (event.key === 'g') header.style.backgroundColor = '#00ff00';
        if (event.key === 'b') header.style.backgroundColor = '#0000ff';
        
        if (event.key === 'h') {
            const currentDisplay = header.style.display;
            header.style.display = currentDisplay === 'none' ? 'block' : 'none';
        }
    }
});
