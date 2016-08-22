-----BEGIN PGP SIGNED MESSAGE-----
Hash: SHA1

Rawfile := DATASET('TST::postcodes', { string8 postcode, UNSIGNED8
                                      __filepos {virtual(fileposition)}}, FLAT);

INDX_Postcode := INDEX(Rawfile, {postcode, __filepos}, 'TST::postcode.key');
BuildIndexOp := BUILDINDEX(INDX_Postcode, OVERWRITE);

BuildIndexOp;
-----BEGIN PGP SIGNATURE-----
Version: GnuPG v1

iQEcBAEBAgAGBQJXutsRAAoJEFFQdhjZhB+ZEqwIAJ7G3zAock/WvKSt5xBfS+Bp
crS+QKCiTkMH/Tm9HoDa5Ub16iSze8NCMtwLn9MLE1NsFbtL+/2VNBbsQ93KHov8
XgfQei8qZPi6sIT+Gn8HkggWCyQamJ8vJc0uQTy8Cos4kK9B52EWs6a8XszUGgqp
89ttWHEF60G1sSexXAuTTleoTvyeZzlm4QPGwJmmyv9Mzn5izx/SlAgdZ9DP9KMM
OyGuVVolU6HxYW0ecfzkb9VXXeuijTwSMkb9rudlucTCK1oYgbA1cmaAyW3nBcm1
aZjOXf0eoe6kFZllMPgex1wN0Ou7LKWA/l5GSPn555C6diOOnRc1lFHbMh3PMvo=
=mA5i
-----END PGP SIGNATURE-----
