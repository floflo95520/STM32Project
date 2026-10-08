## Master
- A réception d'un message INIT avec id_src == null (pc), la carte connectée au PC est donc désignée en tant que maitre, et a la possibilité de générer une clé.

- Sur reception d'un message d'INIT, le maitre génère de manière aléatoire une clé et la transmet aux autres cartes esclaves dans le payload, avec le flag INIT.

- Le maitre reçoit les messages du PC (en clair car c'est le maitre qui a la clé et pas le pc).

- A la réception d'un message de type RESET, le noeud agit comme s'il recevait un message de type INIT de la part du PC (avec un id_src null)

## SLAVE 
- Le noeud doit ignorer tous les octets tant qu'il n'a pas détecté le start_byte.

- A la reception du message d'INIT, si le id_src du header est non null, alors le noeud se configure en tant qu'esclave

- A la reception d'un message INIT, le noeud esclave doit interroger le noeud suivant avec un ACK.

- A la reception d'un retour ACK avec id_src null, le noeud actuel sait qu'il est le dernier noeud (configure une variable d'état), vérifie le calcul d'intégrité du message chiffré, et si ce dernier est bon, déchiffre le message et le transmetn (MSG) en clair au noeud suivant.

- A la réception d'un retour ACK avec id_src non null, le noeud calcule le score CRC sur le payload chiffré et vérifie qu'il obtient bien le même score que le score src déjà enregistré dans la structure du message. S'il obtient le même score, alors il envoie un message avec le même payload au noeud suivant. Sinon, il envoie un message de type ERROR au noeud précédent.

- A la réception d'un message de type ERROR, le noeud transmet l'ERROR au noeud N-1.

- A la réception d'un message de type quelconque sauf ERROR, le noeud renvoie un message de type ACK au noeud duquel il a reçu le message.

## PC
- Le PC doit pouvoir transmettre un MSG (en clair)

- A la reception d'un ACK, le pc doit envoyer un ACK

- A la réception d'un message de type MSG ou ERROR, le pc affiche son payload (en clair car déchiffré par le dernier esclave)


## BOUTON USER
- A 3 appuis de bouton, le noeud transmet au N-1 un message ERR avec dans le payload l'information reset_error


## LED
- Si la clé courante enregistrée lors de l'INIT est nulle (pas d'initialisation pour le moment) alors la LED clignote à 0.5Hz.

- Si la clé courante enregistrée n'est pas nulle alors la LED se fixe et le noeud est synchronisé.

- Si la vérification de l'intégrité du payload (vérification avec crc) échoue, alors la LED clignote à 5Hz