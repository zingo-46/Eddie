# Eddie
Mätnod Jägaren,
Arduino/ESP 8266

Varje nod kan ha en DHT fukt/temp-sensor och 0-flera ow-sensorer

Noden skickar ett start-meddelande till webskriptet.
Den rapporterar sedan varje minut, en sensor per sändning.
En timeout sätter maxtiden för svar från webskriptet till 20 sek. Då returneras en -11 från http-stacken.
En telnet-server ger tillgång till ett antal kommandon:
(log/ver/sens/time/boot/id/blink/ip/tail/reset/exit/title)
log - visar interna loggfilen
sens - visar alla sensorernas id
time - visar nodens tid
boot - startar om noden
id - visar nodens id
blink - blinkar med rött tre gånger
ip - visar nodens IP
tail - visar debuginfo i telnet och IDE
reset - startar om sensorerna
exit - stänger ner telnet-sessionen
title - visar nodens namn i web-servern
