import Std;

rawRecord := RECORD
    unsigned key;
END;

taggedRecord := RECORD(rawRecord)
    unsigned id;
END;

idFileName := 'test::ids';

processInput(dataset(rawRecord) inFile, string outFileName) := FUNCTION
    existingIds := DATASET(idFileName, taggedRecord, THOR);
    unmatchedIds := JOIN(inFile, existingIds, LEFT.key = RIGHT.key, LEFT ONLY, LOOKUP);
    maxId := MAX(existingIds, id);
    newIdFileName := idFileName + (string)maxId;
    newIds := PROJECT(unmatchedIds, TRANSFORM(taggedRecord, SELF.id := maxId + COUNTER; SELF := LEFT));

    tagged := JOIN(inFile, existingIds + newIds, LEFT.key = RIGHT.key, TRANSFORM(taggedRecord, SELF.id := RIGHT.key, SELF := LEFT));

    outputTagged := OUTPUT(tagged,,outFileName, OVERWRITE);
    updateIds := OUTPUT(newIds,,newIdFileName);
    extendSuper := Std.File.AddSuperFile(idFileName, newIdFileName);

    result := SEQUENTIAL(PARALLEL(outputTagged, updateIds), extendSuper) : independent; // actually critical
    RETURN result;
END;


inputDataset := DATASET(100, TRANSFORM(rawRecord, SELF.key := RANDOM() % 10000));
datasetOutsideCritical := inputDataset : independent;


SEQUENTIAL(
    Std.File.CreateSuperFile('idFileName', false, true),
    EVALUATE(datasetOutsideCritical),
    processInput(datasetOutsideCritical, 'test::tagged' + WORKUNIT)
);
