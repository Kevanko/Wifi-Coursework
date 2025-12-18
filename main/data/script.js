function updateData() {
    fetch('/api/data')
        .then(res => res.json())
        .then(data => {
            const currTempDiv = document.getElementById('currTemp');
            currTempDiv.innerText = data.temp.toFixed(1) + ' °C';
            let color = '#03dac6';
            for (let i = 4; i >= 0; i--) {
                if (data.temp >= data.limits[i]) {
                    const led = document.getElementById('t'+i).previousElementSibling.querySelector('.led-indicator');
                    color = led.style.background;
                    break;
                }
            }
            currTempDiv.style.color = color;
            if(document.getElementById('t0').value === '') {
                for(let i=0; i<5; i++)
                    document.getElementById('t'+i).value = data.limits[i].toFixed(1);
            }
        })
        .catch(err => console.error(err));
}




function sendData() {
    let payload = { limits: [] };
    
    for(let i=0; i<5; i++) {
        payload.limits.push(parseFloat(document.getElementById('t'+i).value));
    }
    
    fetch('/api/settings', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify(payload)
    })
    .then(res => { 
        if(res.ok) {
            alert('Настройки сохранены!'); 
            updateData(); 
        } else {
            alert('Ошибка сервера! Код: ' + res.status); 
        }
    })
    .catch(error => { 
        alert('Ошибка соединения или JS!'); 
        console.error('Ошибка POST:', error); 
    });
}

document.addEventListener('DOMContentLoaded', () => {
    setInterval(updateData, 1200); 
    updateData();
});