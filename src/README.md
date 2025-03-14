Gheorghe Marius Razvan 334CA

===TEMA2-APD===


Fiecare proces in afara de tracker(rank = 0), executa 2 thread -uri:
- un thread de download pentru descarcarea chunkurilor solicitate de la peers
- un thread de upload pentru trimiterea chunkurilor, adica pentru a raspunde cererilor de download venite de la clienti

In functia tracker() , in prima parte trackerul va primi informatiile initiale de la clienti,
astfel, trackerul va sti care sunt fisierele din retea, cate segmente au, ın ce ordine sunt, ce hash-uri
au, si cine le detine initial.
Menține două structuri principale:
-file_segments: map<string, vector<string>> file_segments  ce conține hash-urile pentru fiecare fișier
-file_owners: map<string, vector<int>> file_owners ce ține evidența care peers dețin care fișiere

Dupa primirea informatiilor initiale de la clienti, acestora li se va trimite un ACK ca sa stie ca pot continua.
Trackerul proceseaza 3 tipuri de cereri de la clienti:
--TAG_SWARM_REQ: cereri pentru informații despre swarm 
Pentru aceasta cerere raspunde cu peeri pentru fisierul respectiv si hashurile si seteaza 
clientul respectiv ca peer pentru fisier
--TAG_FILE_COMPLETE: notificări când un client termină un fișier
Cand primeste asta adauga clientul ca seed pentru fisierul respectiv
--TAG_ALL_COMPLETE: notificări când un client termină tot
Primeste de la client notificare ca a terminat tot ce avea.
Marcheaza clientul ca terminat si atunci cand termina toti le trimite mesaj de finalizare pentru a se opri

Functia peer(), ruleaza 2 thread -uri, upload si download.

Functia download_thread_func() ,citeste fisierele de input pentru fiecare client cu functia read_file(),
in care ii trimitem trackerului informatiile de care are nevoie, astfel, trackerul va sti cate segmente au, in ce ordine sunt, ce hash-uri au.
Acest map  <owned_files_local> este important pentru că:
Ține evidența ce chunk-uri are peer-ul inițial fiind folosit de thread-ul de upload pentru a răspunde la cereri.
Se actualizează pe măsură ce peer-ul descarcă noi chunk-uri si este protejat cu mutex pentru că este accesat de ambele thread-uri (upload și download).
Cu ajutorul functiei read_wanted_files() citesc fisierele de care am nevoie apoi incep sa le procesez in functia download_thread_func().
Pentru fisierul curent pe care vreau sa il descarc atata timp cat nu a fost completat,cer informatii de la tracker despre swarm  apoi marchez ce chunkuri am deja.
Pentru fiecare segment lipsa ,sortez peer-ii după cât de mult i-am folosit,incercand să ia chunk-ul de la peer-ul cel mai puțin folosit.Daca am folosit un peer pentru un segment il trec in <chunk_peers_tried> pentru a evita cererile la acelasi peer
pentru acelasi chunck.
Trimit cerere la peer pentru a lua chunck ul,daca il primesc de la peer il salvez, incrementez contorul pentru peer sa vedem
de cate ori a fost folosit,incrementez si numarul de download -uri,adaug chunck ul la datele locale pentru a ma ajuta cand il 
vor cere si alti clienti,dar acest lucru il fac sincronizat cu un mutex pentru ca poate fi accesat si de thread ul upload
Verific daca la 10 segmente descarcate trebuie sa cer un update de swarm,daca nu am obtinut chunk ul de la niciun peer
cerem update de swarm.Daca am toate segmentele descarcate le scriu in fisierul de output si ii trimit trackerului mesaj
sa stie ca am terminat.La final daca toate fisierele au fost finalizate ,trimit tag ul ALL_COMPLETE la tracker.

Legat de <eficienta> pentru a nu descarca toate segmentele de la acelasi peer/seed folosec map<string, map<int, int>> peer_usage_count, unde in acest map stochez pentru fiecare fisier peerul si numarul de utilizari per fisier.
Voi sorta peeri dupa numarul de utilizari si cei cu mai putin vor fi primii in lista , fapt ce face sa am o distributie
uniforma ca segmentele sa nu se descarce de la un singur peer/seed,iar atunci cand primesc mesaj cu Ok ca s a gasit segmentul
voi incrementa numarul de utilizari al peerului respectiv pentru fisierul curent.

Functia upload_thread_func(),primim hash ul chunk ului dorit si verificam daca il avem.
Acest thread rulează în paralel cu thread-ul de download șirăspunde la cereri de la alți peers.
Se oprește corect când descărcarea s-a terminat.
