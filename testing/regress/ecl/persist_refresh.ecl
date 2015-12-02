/*##############################################################################

    HPCC SYSTEMS software Copyright (C) 2015 HPCC Systems®.

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

import Std;
import Std.File AS FileServices;

//
//version persistRefresh=true
//version persistRefresh=false

import ^ as root;

string OriginalTextFilesIp := '.' : STORED('OriginalTextFilesIp');
string OriginalTextFilesEclPath := '' : STORED('OriginalTextFilesEclPath');
string OriginalTextFilesOsPath := '' : STORED('OriginalTextFilesOsPath');

persistRefresh := #IFDEFINED(root.persistRefresh, true);
deleteFiles := #IFDEFINED(root.deleteFiles, true);

#IF (persistRefresh=true)
InputFile := OriginalTextFilesOsPath + '/download/europe1.txt';
#ELSE
InputFile := OriginalTextFilesOsPath + '/download/europe2.txt';
#END

ClusterName := 'mythor';
DestFile := '~regress::europe.txt';
ESPportIP := 'http://127.0.0.1:8010/FileSpray';

spray := FileServices.SprayVariable(OriginalTextFilesIp,
                                     InputFile,
                                     destinationGroup := ClusterName,
                                     destinationLogicalName := DestFile,
                                     espServerIpPort := ESPportIP,
                                     ALLOWOVERWRITE := true);

rec := RECORD
    string country;
    integer4 population;
END;

ds := DATASET(DestFile, rec, CSV(HEADING(1), SEPARATOR(','), QUOTE([])));

#IF (persistRefresh=true)
    CountriesDS := ds:PERSIST('~REGRESS::PersistRefresh', SINGLE, REFRESH(true));
#ELSE
    CountriesDS := ds:PERSIST('~REGRESS::PersistRefresh', SINGLE, REFRESH(false));
#END

OutputResults := OUTPUT(CountriesDS);

pause := STD.System.Debug.Sleep(2000);

sequential (
       spray;
       pause; 
       OutputResults;
) ;
