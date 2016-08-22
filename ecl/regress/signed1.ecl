-----BEGIN PGP SIGNED MESSAGE-----
Hash: SHA1

Rawfile := DATASET('TST::postcodes', { string8 postcode, UNSIGNED8
                                      __filepos {virtual(fileposition)}}, FLAT);
INDX_Postcode := INDEX(Rawfile, {postcode, __filepos}, 'TST::postcode.key');

match1 := INDX_Postcode( postcode = 'KT19 1AA' );

OUTPUT(match1);
-----BEGIN PGP SIGNATURE-----
Version: GnuPG v1

iQEcBAEBAgAGBQJXutsyAAoJEFFQdhjZhB+ZZG0IAI3biZME4wzi5mDtnWjJaGjn
dOBaWDQHUuoaI+WAqpmVQFlMupzNcfcxYSdTGamF8gD1AkIB85W4C8061lyPR7DT
3RzMd7F0PLsPuhMd/uBDuJNoRovj5RWI1ZiKd4n1JUnjvTZf42Lt9oD7dyVs784X
Mzh4eT1clYDUFaSyQT7+HKcjuaBw+HZ7em95cRiuehleqUP1RyLUKpxsTx9O+I/7
S2aGvr4x0wON+JmP14L9guwih7i7qYoEeSM+1RTrDgO+3qY3kwGiUBBVrHdfJznj
G9FfjyvWHh1anLHwes/XX0eq/oQn0NLpDX1KhepX47PCQYbE7d3hbbyi78aEIkc=
=QhyZ
-----END PGP SIGNATURE-----
