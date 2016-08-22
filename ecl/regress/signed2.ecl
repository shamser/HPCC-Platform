-----BEGIN PGP SIGNED MESSAGE-----
Hash: SHA1

/*##############################################################################

    HPCC SYSTEMS software Copyright (C) 2016 HPCC Systems®.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and 
    limitations under the License.
############################################################################## */

postcodes := DATASET([{'KT19 1AA'}, {'KT19 1AB'}, {'KT19 1AC'}, {'KT19 1AD'},
                      {'KT20 1AE'}, {'KT20 1AF'}, {'KT20 1AG'}, {'KT20 1AH'}, 
                      {'KT21 1AI'}, {'KT21 1AJ'}, {'KT21 1AK'}, {'KT21 1AL'}, 
                      {'KT22 1AM'}, {'KT22 1AN'}, {'KT22 1AO'}, {'KT22 1AA'},
                      {'KT23 1AB'}, {'KT23 1AC'}, {'KT23 1AD'}, {'KT23 1AE'}, 
                      {'KT30 1AF'}, {'KT30 1AG'}, {'KT30 1AH'}, {'KT30 1AI'},
                      {'KT31 1AJ'}, {'KT31 1AK'}, {'KT31 1AL'}, {'KT31 1AM'}, 
                      {'KT32 1AN'}, {'KT32 1AO'}, {'KT32 1AA'}, {'KT32 1AB'}, 
                      {'KT40 1AC'}, {'KT40 1AD'}, {'KT40 1AE'}, {'KT40 1AF'}, 
                      {'KT41 1AG'}, {'KT41 1AH'}, {'KT41 1AI'}, {'KT41 1AJ'}, 
                      {'KT41 1AK'}, {'KT41 1AL'}, {'KT41 1AM'}, {'KT41 1AN'}, 
                      {'KT3'}, {'KT4'}], {string8 postcode});


outputraw := OUTPUT(postcodes,,'TST::postcodes', OVERWRITE);

outputraw;
-----BEGIN PGP SIGNATURE-----
Version: GnuPG v1

iQEcBAEBAgAGBQJXutsNAAoJEFFQdhjZhB+ZLdsH/0UgHkR41dp9oymBeYXoV3KQ
oJ1/AOOH+U90pAeDlnwkmkRlfXRhiBfKbCOuZULVTSU+4lRCP4gYpKxSWTykOA90
t6jYNBL0osiUyY6PwiFTHocWwv7+VfzXBGN3t81ULBaP3WK2CuFiiNlCR4CMIO+M
rVEflcfVvvwucQPXAMT8+g2kd7vyqg6OIeaMuLZIJ9jcO1S/eHD80WTH4rRzHbY7
qVzfTq1Ot+h3i5S6Dd01DfFTr5D2y2+RnXdVMx4rmKgLh6aGoKmUc69k963naul8
F72y/L+lpiOvf2Rkt6oi31rrOXeNESrKgsXYkSIu/cHIi0yH/Av/o2o7fYD6QUM=
=q5cO
-----END PGP SIGNATURE-----
