#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

#include "khash.h"
#include "utils.h"

// create a mapping between unsorted and sorted persons
KHASH_MAP_INIT_INT(oht, unsigned int);
khash_t(oht) *offset_mapping;

// create a mapping between person id -> offset
KHASH_MAP_INIT_INT64(iht, unsigned int);
khash_t(iht) *id_mapping;

FILE   *person_out;
FILE   *interest_out;
FILE   *knows_out;

Person *person_map, *person_new_map;
Person *person, *new_person;
unsigned int *knows_map;
unsigned short *interest_map;
unsigned long person_length, knows_length;

int location_comparator(const void *v1, const void *v2) {
    Person *r1 = (Person *) v1;
    Person *r2 = (Person *) v2;

    if(r1->location > r2->location)
        return -1;
    if(r1->location < r2->location)
        return +1;
    else
        return 0;
}

int id_comparator(const void *v1, const void *v2) {
    Person *r1 = *(Person **) v1;
    Person *r2 = *(Person **) v2;

    if(r1->person_id < r2->person_id)
        return -1;
    if(r1->person_id > r2->person_id)
        return +1;
    else
        return 0;
}

Person *binary_search(Person **array, unsigned long id, unsigned int low, unsigned int high) {

    if(low > high) {
        return 0;
    }
    if(low == high) {
        return array[low];
    }
    if(high - low == 1) {
        if(array[low]->person_id == id) {
            return array[low];
        } else {
            return array[high];
        }
    }

    unsigned int midIndex = low + ((high-low) / 2);
    unsigned long midId = array[midIndex]->person_id;

    if(id < midId) {
        return binary_search(array, id, low, midIndex);
    } else if(id == midId) {
        return array[midIndex];
    } else {
        return binary_search(array, id, midIndex, high);
    }
}

int main(int argc, char *argv[]) {

    /* output for reorganized knows file */
    char* knows_output_file    = makepath(argv[1], "knows2",    "bin");
    knows_out = open_binout(knows_output_file);

    /* memory-map files created by loader */
    // load original maps
    person_map   = (Person *) mmapr(makepath(argv[1], "person",   "bin"), &person_length);
    knows_map    = (unsigned int *) mmapr(makepath(argv[1], "knows",    "bin"), &knows_length);

    // load all persons in memory
    Person *allPerson = malloc(person_length * sizeof(Person));
    for (unsigned int person_offset = 0; person_offset < person_length/sizeof(Person); person_offset++) {
        allPerson[person_offset] = person_map[person_offset];
    }

    // sort by location
    qsort(allPerson, person_length, sizeof(Person), &location_comparator);

    // write out a binary files
    // and to a hash table
    id_mapping = kh_init(iht);
    char* pers_cl2_output_file = makepath(argv[1], "person_cl2", "bin");
    FILE *p_cl2_out = open_binout(pers_cl2_output_file);

    for (unsigned int person_offset = 0; person_offset < person_length/sizeof(Person); person_offset++) {
        Person *p = &allPerson[person_offset];
        fwrite(p, sizeof(Person), 1, p_cl2_out);
        // put p.id -> offset into a hash table
        int ret;
        khiter_t k;
        k = kh_put(iht, id_mapping, p->person_id, &ret);
        kh_value(id_mapping, k) = person_offset;
    }
    free(allPerson); // done with the sorted array for now

    // init the hash table
    offset_mapping = kh_init(oht);
    for (unsigned int person_offset = 0; person_offset < person_length/sizeof(Person); person_offset++) {

        // read the person from disk
        Person *pDisk = &person_map[person_offset];

        // find the offset in the sorted array
        unsigned int person_offset2 = kh_value(id_mapping, kh_get(iht, id_mapping, pDisk->person_id));

        // put in hash old_offset - > new_offset
        int ret;
        khiter_t k;
        k = kh_put(oht, offset_mapping, person_offset, &ret);
        kh_value(offset_mapping, k) = person_offset2;
    }

    // load all persons in memory (non-sorted)
    allPerson = malloc(person_length * sizeof(Person));
    for (unsigned int person_offset = 0; person_offset < person_length/sizeof(Person); person_offset++) {
        allPerson[person_offset] = person_map[person_offset];
    }
    // have a set of pointers to persons
    Person **personP = malloc(person_length * sizeof(Person *));
    // populate
    for(unsigned int person_offset2 = 0; person_offset2 < person_length/ sizeof(Person); person_offset2++) {
        Person *p = &allPerson[person_offset2];
        personP[person_offset2] = p;
    }
    // sort on id
    qsort(personP, person_length/ sizeof(Person), sizeof(Person *), &id_comparator);

    // reload the maps
    // open person binary for updates (read/write)
    person_new_map = (Person *) mmaprw(makepath(argv[1], "person_cl2",   "bin"), &person_length);

    Person *person, *knows;
    unsigned long knows_file_offset = 0;

    for (unsigned int person_offset = 0; person_offset < person_length/sizeof(Person); person_offset++) {

        // grab a person
        new_person = &person_new_map[person_offset];
        unsigned long new_person_id = new_person->person_id;

        // grab a corresponding person from memory
        unsigned long old_knows_first = 0;
        unsigned short old_knows_n = 0;

        Person *oldPerson = binary_search(personP, new_person_id, 0, person_length/ sizeof(Person));

        old_knows_first = oldPerson->knows_first;
        old_knows_n = oldPerson->knows_n;

        // reset the knows offsets
        new_person->knows_first = knows_file_offset; // current offset in a new knows file
        new_person->knows_n = 0;

        // look in old knows
        // with old person offsets
        for (unsigned long knows_offset = old_knows_first;
             knows_offset < old_knows_first + old_knows_n;
             knows_offset++) {

            // get a friend
            knows = &person_map[knows_map[knows_offset]];

            // check if friend lives in the same city
            if (oldPerson->location != knows->location) {
                continue;
            }

            // find out current friend's person offset
            unsigned int knows_person_offset =
                    kh_value(offset_mapping, kh_get(oht, offset_mapping, knows_map[knows_offset] ));

            // record this friend's name in the new knows file
            fwrite(&knows_person_offset, sizeof(unsigned int), 1, knows_out);

            // increment new knows file offset
            knows_file_offset++;

            // increment number of mutual friends the guy knows
            new_person->knows_n++;
        }

    }

    free(personP);
    free(allPerson);

    return 0;
}

