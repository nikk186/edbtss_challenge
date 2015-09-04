#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

#include "khash.h"
#include "utils.h"

#define PERSON_FIELD_ID 0
#define PERSON_FIELD_BIRTHDAY 4
#define PERSON_FIELD_LOCATION 8
#define KNOWS_FIELD_PERSON 0
#define KNOWS_FIELD_FRIEND 1
#define INTEREST_FIELD_PERSON 0
#define INTEREST_FIELD_INTEREST 1

// hash map needs long keys (large person ids), but unsigned int is enough for person offsets
KHASH_MAP_INIT_INT64(pht, unsigned int)
khash_t(pht) *person_offsets;

FILE   *person_out;
FILE   *interest_out;
FILE   *knows_out;
FILE    *p2_out;

Person *person_map, *person_new_map;
Person *person, *new_person;
unsigned int *knows_map;
unsigned short *interest_map;
unsigned long person_length, knows_length;

int main(int argc, char *argv[]) {

    /* output for reorganized knows file */
    char* knows_output_file = makepath(argv[1], "knows2", "bin");
    knows_out = open_binout(knows_output_file);

    char* person2_output_file = makepath(argv[1], "person_cl2", "bin");
    p2_out = open_binout(person2_output_file);

    person_map   = (Person *)         mmapr(makepath(argv[1], "person",   "bin"), &person_length);

    for (unsigned int person_offset = 0; person_offset < person_length/sizeof(Person); person_offset++) {
        // grab a person
        person = &person_map[person_offset];
        fwrite(person, sizeof(Person), 1, p2_out);
    }
    fclose(p2_out);

    /* memory-map files created by loader */
    // open person binary for updates (read/write)
    person_new_map   = (Person *)         mmaprw(makepath(argv[1], "person_cl2",   "bin"), &person_length);
    knows_map    = (unsigned int *)   mmapr(makepath(argv[1], "knows",    "bin"), &knows_length);

    Person *person, *knows;
    unsigned long knows_file_offset = 0;

    for (unsigned int person_offset = 0; person_offset < person_length/sizeof(Person); person_offset++) {

        // grab a person
        person = &person_map[person_offset];
        new_person = &person_new_map[person_offset];

        // grab old offsets in knows
        unsigned long old_knows_first = person->knows_first;
        unsigned short old_knows_n = person->knows_n;

        // reset the knows offsets
        new_person->knows_first = knows_file_offset; // current offset in a new knows file
        new_person->knows_n = 0;

        for (unsigned long knows_offset = old_knows_first;
             knows_offset < old_knows_first + old_knows_n;
             knows_offset++) {

            // get a friend
            knows = &person_map[knows_map[knows_offset]];

            // check if friend lives in same city
            if (person->location != knows->location) {
                continue;
            }

            // find out current friend's person offset
            unsigned int knows_person_offset = knows_map[knows_offset];

            // record this friend's name in the new knows file
            fwrite(&knows_person_offset, sizeof(unsigned int), 1, knows_out);

            // increment new knows file offset
            knows_file_offset++;

            // increment number of mutual friends the guy knows
            new_person->knows_n++;
        }

    }

    return 0;
}

