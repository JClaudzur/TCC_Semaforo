Este é um Trabalho de Conclusão de Curso, sobre semáforos inteligentes que utilizam câmeras para ajustar os sinais de transito conforme a quantidade de carros.

O sistema proposto opera de forma integrada entre dispositivos embarcados e um computador central, combinando visão computacional e controle automatizado. 
Duas ESP32-CAMs são posicionadas em pontos distintos do cruzamento, cada uma responsável por capturar imagens do fluxo de veículos em sua respectiva via.
Você pode utilizar com apenas uma camera - Para a via A - e o código randomizando números e enviando para a via B. 
As imagens capturadas são transmitidas via rede Wi-Fi para um computador, onde são processadas utilizando as bibliotecas OpenCV e o modelo YOLO (You Only Look Once) 
— capazes de detectar e contar veículos em tempo real. Com base na análise feita pelo computador, são enviados comandos para um módulo ESP32, que atua como controlador dos semáforos físicos (representados por LEDs). 
O ESP32 ajusta automaticamente o tempo dos sinais conforme o número de veículos detectados em cada via, priorizando a liberação da via com maior fluxo e reduzindo o tempo ocioso em cruzamentos menos movimentados. 
Essa abordagem permite uma central de controle inteligente para múltiplos cruzamentos, combinando baixo custo com tecnologias de visão artificial de alta precisão.
