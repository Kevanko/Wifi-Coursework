function updateData() {
    fetch('/api/data')
        .then(response => response.json())
        .then(data => {
            // 1. Обновляем температуру
            document.getElementById('currTemp').innerText = data.temp.toFixed(1) + ' °C';
            
            // 2. Обновляем поля настроек только при первой загрузке
            // Проверяем t0, чтобы избежать затирания введенных пользователем данных
            if(document.getElementById('t0').value === '') {
                for(let i=0; i<5; i++) {
                    document.getElementById('t'+i).value = data.limits[i].toFixed(1);
                }
            }
        })
        .catch(error => { console.error('Ошибка GET данных:', error); });
}

function sendData() {
    let payload = { limits: [] };
    
    // Собираем данные из полей ввода
    for(let i=0; i<5; i++) {
        // Парсим в float
        payload.limits.push(parseFloat(document.getElementById('t'+i).value));
    }
    
    fetch('/api/settings', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json' // Важно для ESP32
        },
        body: JSON.stringify(payload)
    })
    .then(res => { 
        if(res.ok) {
            alert('Настройки сохранены!'); 
            // Принудительно запускаем обновление, чтобы поля не опустели
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

// Запуск при загрузке страницы и каждые 2 секунды
document.addEventListener('DOMContentLoaded', () => {
    setInterval(updateData, 2000); 
    updateData();
});