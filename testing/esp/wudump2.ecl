IMPORT Std.File;
IMPORT Std.Str;
IMPORT Std.System.Workunit as wu;
IMPORT ws_workunits;

// Configuration 
#DECLARE (show_request);
#SET (show_request, false);
espurl := File.GetEspURL(username := '', userPW := '');
workuniturl := espurl + '/WsWorkunits/WUDetails?ver_=1.71';

t_JobList := RECORD
  STRING20 wuid;
END;

jobwuid := '' : STORED('jobwuid');
jobList := DATASET([{jobwuid}], t_JobList);

// Build Test cases
propFilterEmpty := DATASET([], ws_workunits.t_WUPropertyFilter);
scopeFilter := DATASET([{999,[],[],[], propFilterEmpty}], ws_workunits.t_WUScopeFilter);
nestedFilter := DATASET([{999,[]}], ws_workunits.t_WUNestedFilter);

t_testCase := RECORD
    string Name;
    ws_workunits.t_WUScopeFilter ScopeFilter {xpath('ScopeFilter')};
    ws_workunits.t_WUNestedFilter NestedFilter {xpath('NestedFilter')};
    ws_workunits.t_WUPropertiesToReturn PropertiesToReturn {xpath('PropertiesToReturn')};
    string Filter {xpath('Filter')};
    ws_workunits.t_WUScopeOptions ScopeOptions {xpath('ScopeOptions')};
    ws_workunits.t_WUPropertyOptions PropertyOptions {xpath('PropertyOptions')};
END;

testCase1 := ROW({ 'Default',
                   {999,[],[],[], propFilterEmpty},
                   {999,[]},
                   {1,1,1,0,'ts',[],[]},
                   '',
                   {1, 1, 1, 1},
                   {1,1,1,1,1,1} }, t_testCase);
testCase2 := ROW({ 'MaxDepth=1, NestedDepth=1',
                   {1,[],[],[], propFilterEmpty},
                   {1,[]},
                   {0,0,0,0,'ts',[],[]},
                   '',
                   {1, 1, 1, 1},
                   {1,1,1,1,1,1} }, t_testCase);
testCase3 := ROW({ 'MaxDepth=1, ScopesType=graph',
                   {1,[],[],['graph1'], propFilterEmpty},
                   {1,[]},
                   {0,0,0,0,'ts',[],[]},
                   '',
                   {1, 1, 1, 1},
                   {1,1,1,1,1,1} }, t_testCase);
testCase4 := ROW({ 'MaxDepth=1, ScopeType=subgraph',
                   {1,[],[],['subgraph'], propFilterEmpty},
                   {1,[]},
                   {0,0,0,0,'ts',[],[]},
                   '',
                   {1, 1, 1, 1},
                   {1,1,1,1,1,1} }, t_testCase);
testCase5 := ROW({ 'MaxDepth=999, ScopeType=subgraph',
                   {999,[],[],['subgraph'], propFilterEmpty},
                   {1,[]},
                   {0,0,0,0,'ts',[],[]},
                   '',
                   {1, 1, 1, 1},
                   {1,1,1,1,1,1} }, t_testCase);
testCase6 := ROW({ 'MaxDepth=999, Scope=graph1',
                   {999,['graph1'],[],[], propFilterEmpty},
                   {1,[]},
                   {0,0,0,0,'ts',[],[]},
                   '',
                   {1, 1, 1, 1},
                   {1,1,1,1,1,1} }, t_testCase);
                   // add more test cases here
testCases := DATASET( [testCase1,testCase2,testCase3, testCase4, testCase5, testCase6], t_testCase);

// Request structure
t_WUDetailRequest := RECORD
    string Name;
    string WUID {xpath('WUID')};
    ws_workunits.t_WUScopeFilter ScopeFilter {xpath('ScopeFilter')};
    ws_workunits.t_WUNestedFilter NestedFilter {xpath('NestedFilter')};
    ws_workunits.t_WUPropertiesToReturn PropertiesToReturn {xpath('PropertiesToReturn')};
    string Filter {xpath('Filter')};
    ws_workunits.t_WUScopeOptions ScopeOptions {xpath('ScopeOptions')};
    ws_workunits.t_WUPropertyOptions PropertyOptions {xpath('PropertyOptions')};
end;

t_WUDetailRequest WuDetailsTransform (t_JobList L, t_testCase R) := TRANSFORM
    SELF.Name := R.Name;
    SELF.wuid := L.wuid;
    SELF.ScopeFilter := R.ScopeFilter;
    SELF.NestedFilter := R.NestedFilter; 
    SELF.PropertiesToReturn := R.PropertiesToReturn;
    SELF.Filter := R.Filter;
    SELF.ScopeOptions := R.ScopeOptions;  
    SELF.PropertyOptions := R.PropertyOptions; 
END;

// Create request dataset
wudetailsRequest := JOIN(jobList, testCases, TRUE, WuDetailsTransform(LEFT,RIGHT), ALL);

// Submit request and generate results dataset 
t_WUDetailRequest defParams(t_WUDetailRequest l) := TRANSFORM
    SELF := L;
END;

results := SOAPCALL(wudetailsRequest, workuniturl,'WUDetails', t_WUDetailRequest, defParams(LEFT), DATASET(ws_workunits.t_WUDetailsResponse), XPATH('WUDetailsResponse') );

SET OF STRING maskValueForProperties := ['WhenCreated','WhenCompiled','WhenStarted','WhenFinished','Definition'];

// Mask out fields that shouldn't be used for comparison
ws_workunits.t_WUDetailsResponse maskOutFields(ws_workunits.t_WUDetailsResponse L) := TRANSFORM
    SELF.MaxVersion := IF(L.MaxVersion=0, 0, 123456789);
    SELF.WUID := L.WUID;

    maskOutFormatted(string origFormatted, string origMeasure, string Name) := FUNCTION
        return IF(name='Definition', REGEXREPLACE('.*/',origFormatted,'(path masked)/'),
                  CASE(origMeasure, 'ts' => REGEXREPLACE('\\d',origFormatted,'x'), origFormatted));
    END;

    maskOutRaw(string origRaw, string Name) := FUNCTION
        return IF(Name IN maskValueForProperties, IF(name='Definition',
                  REGEXREPLACE('.*/',origRaw,'(path masked)/'),
                  REGEXREPLACE('\\d+',origRaw,'(raw masked)')), origRaw);
    END;


    ws_workunits.t_WUResponseScope maskOutScopes(ws_workunits.t_WUResponseScope L) := TRANSFORM
        ws_workunits.t_WUResponseProperty maskOutProperty(ws_workunits.t_WUResponseProperty  L) := TRANSFORM
            SELF.RawValue :=  maskOutRaw(L.RawValue, L.Name);
            SELF.Formatted := maskOutFormatted(L.Formatted, L.Measure, L.Name); 
            SELF.Creator := REGEXREPLACE('@.*', L.creator, '@(ip address masked)'); 
            SELF := L;
        END;

        SELF.Properties := PROJECT(L.Properties, maskOutProperty(LEFT));
        SELF := L;
    END;

    SELF.Scopes := PROJECT(L.Scopes,maskOutScopes(LEFT));
END;

resultsMasked := PROJECT(results, maskOutFields(LEFT));

// Add some extra header info
t_Header := RECORD
    string TestName {xpath('TestName')};
#IF (%show_request% = true)
    t_WUDetailRequest Request {xpath('Request')};
#END
END;

t_Results := RECORD
    t_Header Header {xpath('Header')};
    ws_workunits.t_WUDetailsResponse response {xpath('Response')};
END;

t_Results TransformCombine(ws_workunits.t_WUDetailsResponse L, t_WUDetailRequest R) := TRANSFORM
    SELF.Header.TestName := R.Name;
#IF (%show_request% = true)
    SELF.Header.Request := R;
#END
    SELF.response := L;
END;

finalResults := Combine(resultsMasked,wudetailsRequest,TransformCombine(LEFT,RIGHT));

OUTPUT(finalResults);
