IMPORT Std.File;
IMPORT Std.Str;
IMPORT Std.System.Workunit as wu;
IMPORT ws_workunits;

// Configuration 
espurl := File.GetEspURL(username := '', userPW := '');
workuniturl := espurl + '/WsWorkunits/WUDetails?ver_=1.71';

// Get list of required workunits
requiredWUJobs := SORT(DATASET([{'keyed_denormalize-multiPart(true)'},
                                {'selfjoinlw'},
                                {'sort'},
                                {'loopall'},
                                {'keydiff1'},
                                {'keydiff'},
                                {'key'},
                                {'kafkatest'}], {STRING40 queryname} ), queryname);

wulist := sort(wu.WorkunitList(username:='regress', state := 'completed'),-wuid);

t_NewWorkUnitRecord := RECORD(wu.WorkunitRecord)
  STRING40 queryname;
END;

// Workout queryname from job name
t_NewWorkUnitRecord wutransform (wu.WorkunitRecord L, requiredWUJobs R) := TRANSFORM
  SELF.queryname := R.queryname;
  SELF := L;
END;

reqJobs := JOIN(wulist, requiredWUJobs,LEFT.job[1..LENGTH(TRIM(LEFT.job))-14]=RIGHT.queryname, wutransform(LEFT,RIGHT), INNER, ALL);

// Only retain the newest jobs
newestJobs := DEDUP(reqJobs, queryname, cluster);

OUTPUT(newestJobs);
