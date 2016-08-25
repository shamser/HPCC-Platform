rec := RECORD
  unsigned1 personid;
  string15 firstname;
  string25 lastname;
  unsigned8 age;
  string25 city;
END;

person1 := DATASET('~people1',rec,THOR);
person2 := DATASET('~people2',rec,THOR);
person3 := DATASET('~people3',rec,THOR);
person4 := DATASET('~people4',rec,THOR);
person5 := DATASET('~people5',rec,THOR);

count(person4+person5);
count(person1+person2+person3);
