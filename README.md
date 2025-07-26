# Eddie
Mätnod Jägaren,
Arduino/ESP 8266

Varje nod kan ha en DHT fukt/temp-sensor och 0-flera ow-sensorer

Noden skickar ett start-meddelande till webskriptet.
Den rapporterar sedan varje minut, en sensor per sändning.
En timeout sätter maxtiden för svar från webskriptet till 20 sek. Då returneras en -11 från http-stacken.
En telnet-server ger tillgång till ett antal kommandon:
(log/ver/sens/time/boot/id/blink/ip/tail/reset/exit/title): <br>
log - visar interna loggfilen  <br>
sens - visar alla sensorernas id  <br>
time - visar nodens tid  <br>
boot - startar om noden  <br>
id - visar nodens id  <br>
blink - blinkar med rött tre gånger  <br>
ip - visar nodens IP  <br>
tail - visar debuginfo i telnet och IDE  <br>
reset - startar om sensorerna  <br>
exit - stänger ner telnet-sessionen  <br>
title - visar nodens namn i web-servern  <br>
