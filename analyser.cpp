#include "analyser.h"
#include <QTextStream>
#include "simmanager.h"
#include <QDebug>
#include <QHash>

#include "mainwindow.h"
#include <QSet>

species::species()
{
    type=0;
    ID=0; //default, not assigned
    internalID=-1;
    parent=0;
    size=-1;
    origintime=-1;
}

Analyser::Analyser()
{
    genomes_total_count=0;
}

void Analyser::AddGenome_Fast(quint64 genome)
{
    //adds genome to sorted list. Use halving algorithm to find insertion point

    if (genome_list.count()==0)
    {
        genome_list.append(genome);
        genome_count.append(1);
        genomes_total_count++;
        return;
    }


    int minp=0;
    int maxp=genome_list.count()-1;
    int lookingat=(maxp+minp)/2; //start in middle

    int counter=0;
    while(counter++<100)
    {
        if (genome_list[lookingat]==genome)
        {
            genome_count[lookingat]++;
            genomes_total_count++;
            return;
        } //found it

        //found where it should be?

        if (genome_list[lookingat]<genome)
        {
            //we are lower in list than we should be - genome we are looking at
            //is lower than us
            int next=lookingat+1;
            if (next==genome_count.count()) //at end, just append then
            {
                genome_list.append(genome);
                genome_count.append(1);
                genomes_total_count++;
                goto done;
            }

            if (genome_list[next]>genome)
            {
                //insert here
                genome_list.insert(next,genome);
                genome_count.insert(next,1);
                genomes_total_count++;
                goto done;
            }
            else
            {
                minp=lookingat;
                int oldlookingat=lookingat;
                lookingat=(maxp+minp)/2;
                if (lookingat==oldlookingat) lookingat++;
            }
        }
        else
        {
            //we are higher in list than we should be - genome we are looking at
            //is higher than us


            int next=lookingat-1;
            if (next==-1) //at start, just prepend then
            {
                genome_list.prepend(genome);
                genome_count.prepend(1);
                genomes_total_count++;
                goto done;
            }

            if (genome_list[next]<genome)
            {
                //insert here
                genome_list.insert(lookingat,genome);
                genomes_total_count++;
                genome_count.insert(lookingat,1);
                goto done;
            }
            else
            {
                maxp=lookingat;
                lookingat-=(maxp+minp)/2;
                int oldlookingat=lookingat;
                lookingat=(maxp+minp)/2;
                if (lookingat==oldlookingat) lookingat--;
            }
        }
    };

    //shouldn't get here - old emergency test!
    if (counter>=99) qDebug()<<"Fell off end, counter is "<<counter;
done:
    return;
}



void Analyser::Groups_With_History_Modal()
//Implementation of new search mechanism based around using modal genome as core of species
/*

1. Take ordered genomes from addgroups_fast
2. Set up an array for each which has a species number - default is 0 (not assigned). Set N (next species number) to 1
3. Find largest count not yet assigned to a species (perhaps do by sorting, but probably fine to just go through and find biggest). If everything is assigned, go to DONE
4. Compare this to ALL other genomes, whether assigned to a species or not. If they are close enough, mark them down as species N (will include self), or if they are already assigned to a species, note somewhere that those two species are equivalent
5. go through and fix up equivalent species
6. Go to 3

Species still merge a little too easily.
How about - only implement a species merge if we find a decent number of connections
Now does this, but looks at not absolute size but connection size wrt overall size of the to-be-merged species
Setting in dialog for sensitivity is percentage ratio between link size and my size. Normally around 100 - so small will tend to link to big, but
big won't link to small. Seems to work pretty well.


Next modification - matching up with species from different time-slices


Then - do comparison with last time - not yet implemented
*/
{

    //QTime t;
    //t.start();

    //We start with two QLlists
    // genome_list - list of quint64 genomes
    // genome_count - list of ints, the number of occurences of each
    // genomes_total_count is the sum of values in genome_count

    //2. Set up and  blank the species_id array
    int genome_list_count=genome_list.count();
    QList<int> species_sizes;
    QList<int> species_type; //type genome in genome_list, genome_count

    species_sizes.append(0); //want an item 0 so we can start count at 1#
    species_type.append(0);

    //Calculate mergethreshold - how many cross links to merge a species?

    species_id.clear();
    for (int i=0; i<genome_list_count; i++) species_id.append(0);

    //This is N above
    int next_id=1;

    //List to hold the species translations
    QHash<int,int> merge_species; //second int is count of finds


    //3. Find largest count
    do
    {
        int largest=-1;
        int largest_index=-1;
        for (int i=0; i<genome_list_count; i++)
        {
            if (species_id[i]==0)
            if (genome_count[i]>largest)
            {
                largest=genome_count[i];
                largest_index=i;
            }
        }

        if (largest==-1) break; // if all assigned - break out of do-while

        //qDebug()<<"Species "<<next_id<<" size is "<<largest;
        //4. Compare this to ALL other genomes, whether assigned to a species or not.
        //If they are close enough, mark them down as species N (will include self),
        //or if they are already assigned to a species, note somewhere that those two species are equivalent

        int this_species_size=0;
        quint64 mygenome=genome_list[largest_index];
        for (int i=0; i<genome_list_count; i++)
        {
            quint64 g1x = mygenome ^ genome_list[i]; //XOR the two to compare
            quint32 g1xl = quint32(g1x & ((quint64)65536*(quint64)65536-(quint64)1)); //lower 32 bits
            int t1 = bitcounts[g1xl/(quint32)65536] +  bitcounts[g1xl & (quint32)65535];
            if (t1<=maxDiff)
            {
                quint32 g1xu = quint32(g1x / ((quint64)65536*(quint64)65536)); //upper 32 bits
                t1+= bitcounts[g1xu/(quint32)65536] +  bitcounts[g1xu & (quint32)65535];
                if (t1<=maxDiff)
                {
                    if (species_id[i]>0)  //already in a species, mark to merge- summing the number of genome occurences creating the link
                    {
                        int key=species_id[i];
                        int sum=genome_count[i];
                        if (merge_species.contains(key)) sum+=merge_species[key];
                        merge_species.insert(species_id[i],sum);
                    }
                    else
                    {
                        this_species_size+=genome_count[i]; // keep track of occurences
                        species_id[i]=next_id;
                    }
                }
            }
        }


        //5. go through and fix up equivalent species
        //merge any species that need merging
        //iterate over set, convert all examples to this species
        //Also check for the correct type specimen for this species - the one with highest count basically

        QHashIterator<int,int> j(merge_species);
        int highestcount=largest;
        int highestcountindex=largest_index;
        while (j.hasNext())
        {
            j.next();
            int tomerge=j.key();
            //OLD int senscalc= ((j.value())*100) / this_species_size; //work out percentage of link size to species size
            int usesize = qMin(this_species_size,species_sizes[tomerge]); //use ratio of links to SMALLEST of the two populations
            int senscalc= ((j.value())*100) / usesize;
            //qDebug()<<"senscalc "<<senscalc<<"  size of link is "<<j.value()<<"  "<< " size of this species is "<<this_species_size<< " size of other species is "<<species_sizes[tomerge];
            if (senscalc >= speciesSensitivity)
           {

                //qDebug()<<"Merging "<<tomerge<<" ...";

                if (genome_count[species_type[tomerge]]>highestcount)
                {
                    highestcount=genome_count[species_type[tomerge]];
                    highestcountindex=species_type[tomerge];
                }

                this_species_size+=species_sizes[tomerge];
                species_sizes[tomerge]=0; //merged species size to 0
                for (int i=0; i<genome_list_count; i++)
                    if (species_id[i]==tomerge) species_id[i]=next_id;
            }
        }

        merge_species.clear();
        species_type.append(highestcountindex);
        species_sizes.append(this_species_size); //store size
//        qDebug()<<"species sizes: "<<species_sizes;
        next_id++;
    } while (true);

//    qDebug()<<"Done species fine, elapsed "<<t.elapsed();


    //OK, now to match up with last time

    //Actual results from all this are
    //  species_type - array of indexes to type specimens
    //  species_sizes - total size for each species (if 0, not a species)
    //  Want to turn this into a Qlist of species structures - each an ID and a modal genome

    QList<species> newspecieslist;

    for (int i=1; i<species_sizes.count(); i++)
    {
        if (species_sizes[i]>0)
        {
            species s;
            s.type=genome_list[species_type[i]];
            s.internalID=i;
            s.size=species_sizes[i];
            newspecieslist.append(s);
        }
    }


    //That's created the new list. Now need to go through and match up with the old one. Use code from before... reproduced
    //below

    //set up parent/child arrays - indices will be group keys
    QHash <int,int> parents; //link new to old. Key is specieslistnew indices, value is specieslistold index.
    QHash <int,int> childdists; //just record distance to new - if find better, replace it. Key is specieslistold indices
    QHash <int,int> childcounts; //Number of children
    QHash <int,int> primarychild; //link old to new
    QHash <int,int> primarychildsizediff; //used for new tiebreaking code. Key is specieslistold indices, value is size difference to new

    QList<species> oldspecieslist_combined=oldspecieslist;

    //Add in all past lists to oldspecieslist  - might be slow, but simplest solution. Need to do an add that avoids duplicates though
    QSet<quint64> IDs;

    //put all IDs in the set from oldspecieslist
    for (int i=0; i<oldspecieslist.count(); i++) IDs.insert(oldspecieslist[i].ID);

    //now append all previous list items that are not already in list with a more recent ID!
    for (int l=0; l<(timeSliceConnect-1) && l< archivedspecieslists.count(); l++)
        for (int m=0; m<archivedspecieslists[l].count(); m++)
        {
            if (!(IDs.contains(archivedspecieslists[l][m].ID)))
            {
                IDs.insert(archivedspecieslists[l][m].ID);
                oldspecieslist_combined.append(archivedspecieslists[l][m]);
            }
        }


    if (oldspecieslist.count()>0)
    {

        for (int i=0; i<newspecieslist.count(); i++)
        {
            //for every new species

            int bestdist=999;
            int closestold=-1;
            int bestsize=-1;

            //look at each old species, find closest
            for (int j=0; j<oldspecieslist_combined.count(); j++)
            {
                quint64 g1x = oldspecieslist_combined[j].type ^ newspecieslist[i].type; //XOR the two to compare
                quint32 g1xl = quint32(g1x & ((quint64)65536*(quint64)65536-(quint64)1)); //lower 32 bits
                int t1 = bitcounts[g1xl/(quint32)65536] +  bitcounts[g1xl & (quint32)65535];
                quint32 g1xu = quint32(g1x / ((quint64)65536*(quint64)65536)); //upper 32 bits
                t1+= bitcounts[g1xu/(quint32)65536] +  bitcounts[g1xu & (quint32)65535];

                if (t1==bestdist)
                {
                    //found two same distance. Parent most likely to be the one with the bigger population, so tiebreak on this!
                    if (oldspecieslist_combined[j].size>bestsize)
                     {bestdist=t1; closestold=j; bestsize=oldspecieslist_combined[j].size;}
                }
                if (t1<bestdist) {bestdist=t1; closestold=j; bestsize=oldspecieslist_combined[j].size;}
            }

            parents[i]=closestold; //record parent
            int thissizediff=qAbs(bestsize - newspecieslist[i].size);

            if (childdists.contains(closestold))  //already has a child
            {

                if (thissizediff<primarychildsizediff[closestold]) //one closest in size is to be treated as primary

                  {childdists[closestold]=bestdist; primarychild[closestold]=i; primarychildsizediff[closestold]=thissizediff;}
/*
                //if (childdists[closestold]>bestdist) {childdists[closestold]=bestdist; primarychild[closestold]=i;}
                if (childdists[closestold]==bestdist)
                {
                    //tie-break on size again - primary child should be the child closest in size to original I think

                    if (thissizediff<primarychildsizediff[closestold])
                      {childdists[closestold]=bestdist; primarychild[closestold]=i; primarychildsizediff[closestold]=thissizediff;}
                }
*/
                childcounts[closestold]=childcounts[closestold]+1;
            }
            else //new
            {

                childdists[closestold]=bestdist;
                childcounts[closestold]=1;
                primarychild[closestold]=i;
                primarychildsizediff[closestold]=thissizediff;
            }
        }

        //now handle ID numbers. Loop over old
        for (int j=0; j<oldspecieslist_combined.count(); j++)
        {
            //for every old species

            if (childcounts.contains(j))
            {
                newspecieslist[primarychild[j]].ID=oldspecieslist_combined[j].ID;
                newspecieslist[primarychild[j]].parent=oldspecieslist_combined[j].parent;
                newspecieslist[primarychild[j]].origintime=oldspecieslist_combined[j].origintime;

            }
//                else //apparently went extinct, currently do nothing
//                qDebug()<<"Species ID "<<oldspecieslist_combined[j].ID<<" apparently extinct: Size was "<<oldspecieslist_combined[j].size<<"  Time alive was "<<generation-oldspecieslist_combined[j].origintime;

        }

        //fill in blanks - new species
        for (int i=0; i<newspecieslist.count(); i++)
        if (newspecieslist[i].ID==0)
        {
            newspecieslist[i].ID=nextspeciesid++;
            newspecieslist[i].parent=oldspecieslist_combined[parents[i]].ID;
            newspecieslist[i].origintime=generation;


        }

    }
    else
    {
        //handle first time round - basically give species proper IDs
        for (int i=0; i<newspecieslist.count(); i++)
        {
            newspecieslist[i].ID=nextspeciesid++;
            newspecieslist[i].origintime=generation;
        }
    }
//    qDebug()<<"Done species linking, elapsed "<<t.elapsed();

    //straighten out parents in new array, and any other persistent info
    QHashIterator<int,int> ip(parents);

    while (ip.hasNext())
    {
        ip.next();
        //link new to old. Key is specieslistnew indices, value is specieslistold index.
        if (newspecieslist[ip.key()].parent == 0) //not a new parent, so must be yet-to-be filled from parent - anagenetic descendent
            newspecieslist[ip.key()].parent = oldspecieslist_combined[ip.value()].parent;
        if (newspecieslist[ip.key()].origintime == -1) //not a new time, so must be yet-to-be filled from parent
            newspecieslist[ip.key()].origintime = oldspecieslist_combined[ip.value()].origintime;
    }

    //finally go through newspecieslist and look at internalID - this is ID in the species_ID array
    //do same for colour array
    lookup_persistent_species_ID.clear();

    for (int i=0; i<=species_id.count(); i++) lookup_persistent_species_ID.append(0);


    for (int i=0; i<newspecieslist.count(); i++) lookup_persistent_species_ID[newspecieslist[i].internalID]=newspecieslist[i].ID;


    //handle archive of old species lists (prior to last time slice)
    if (oldspecieslist.count()>0 && timeSliceConnect>1)  //if there IS an old species list, and if we are storing them
    {
        archivedspecieslists.prepend(oldspecieslist); //put the last old one in position 0
        while (archivedspecieslists.count()>timeSliceConnect-1) archivedspecieslists.removeLast(); //trim list to correct size
                                // will normally only remove one from end, unless timeSliceConnect has changed
                                // TO DO - note species going extinct here?
    }
    oldspecieslist=newspecieslist;
//    qDebug()<<"Done last bits, elapsed "<<t.elapsed();
}

int Analyser::SpeciesIndex(quint64 genome)
//returns index (in genome_count, genome_list, species_id) for genome
{
    QList<quint64>::iterator i = qBinaryFind(genome_list.begin(),genome_list.end(), genome);

    if (i==genome_list.end())
        return -1;
    else
        return lookup_persistent_species_ID[species_id[i - genome_list.begin()]]; // this is QT bodgy way to get index apparentlty
}








//----------------------------------------------------------------------
//OLDER CODE BELOW - for older reporting system - probably now obsolete
//---------------------------------------------------------------------




















void Analyser::AddGenome(quint64 genome, int fitness)
{
    for (int j=0; j<genomes.count(); j++)
    {
        if (genomes[j].genome==genome) {genomes[j].count++; return;}
    }
    genomes.append(sortablegenome(genome, fitness,1));
}

QString Analyser::SortedSummary()
//Pass this a list of genomes pre-grouped
{
    QString line;
    QTextStream sout(&line);

    qSort(genomes.begin(),genomes.end());

    for (int i=0; i<genomes.count(); i++)
    {
        if (genomes[i].count<10) sout<<" "<<genomes[i].count<<": "; else  sout<<genomes[i].count<<": ";
        for (int j=0; j<32; j++)
            if (tweakers64[63-j] & genomes[i].genome) sout<<"1"; else sout<<"0";
        sout<<" ";
        for (int j=32; j<64; j++)
            if (tweakers64[63-j] & genomes[i].genome) sout<<"1"; else sout<<"0";

        sout<<"  fitness: "<<genomes[i].fit;

        sout<<"\n";
    }
    return line;
}

int Analyser::Spread(int position, int group)
//Old version of function
//return value for next group!
{
    QList <int> joingroups;

    genomes[position].group=group;
    quint64 mygenome=genomes[position].genome;

    for (int i=0; i<genomes.count(); i++)
    {
        if (joingroups.indexOf(genomes[i].group)==-1)
        {
            quint64 g1x = mygenome ^ genomes[i].genome; //XOR the two to compare
            quint32 g1xl = quint32(g1x & ((quint64)65536*(quint64)65536-(quint64)1)); //lower 32 bits
            int t1 = bitcounts[g1xl/(quint32)65536] +  bitcounts[g1xl & (quint32)65535];
            if (t1<=maxDiff)
            {
                quint32 g1xu = quint32(g1x / ((quint64)65536*(quint64)65536)); //upper 32 bits
                t1+= bitcounts[g1xu/(quint32)65536] +  bitcounts[g1xu & (quint32)65535];
                if (t1<=maxDiff)
                {
                    if (genomes[i].group==0) genomes[i].group=group;
                    else
                    {
                        if (i!=position)  {joingroups.append(genomes[i].group); unusedgroups.append(genomes[i].group);}
                    }
                }
            }
        }
    }

    for (int i=0; i<genomes.count(); i++) if (joingroups.indexOf(genomes[i].group)!=-1) genomes[i].group=group;
    //OK, should now
    //newnum should be lowest group to use next time
    return group+1;
}


QString Analyser::Groups()
//Works out species
//Pass this a list of genomes pre-grouped
{
    if (genomes.count()==0) return "Nothing to analyse";

    //Sort the genomes. There are 10000 of these - might be slow - but it does seem to speed matters
    qSort(genomes.begin(),genomes.end());

    //make sure the 'unused' list is empty - this list is going to contain indices of lists that have been amalgamated and are no longer needed
    unusedgroups.clear();

    //start flood-fill style spread algorithm on position 0, for group 1. Basically go through all genomes that are unplaced and place them
    int group=1; //first group to use (0 means ungrouped).
    bool done=false;
    while (done==false) //keep going until all are placed
    {
        done=true;
        for (int i=0; i<genomes.count(); i++)
            if (genomes[i].group==0)
            {
                group=Spread(i,group); //return next group number (current one +1). This is the recursive algorithm that assigns all genomes it can to the group
                done=false; break;
            }
    }


    //Sort out the list - handle all the joined groups basically. Nothing too computationally expensive here
    qSort(unusedgroups.begin(), unusedgroups.end());
    unusedgroups.append(-1); //to avoid check failing
    //Unused groups now holds
    QList <int> GroupTranslate;
    int unusedpos=0;
    int realpos=1;
    GroupTranslate.append(-1);
    for (int i=1; i<=group; i++)
    {
        if (unusedgroups[unusedpos]==i)
        {
            GroupTranslate.append(-1);
            unusedpos++;
        }
        else
            GroupTranslate.append(realpos++);
    }


    QString line;
    QTextStream sout(&line);

/*
    for (int i=0; i<genomes.count(); i++)
    {
        if (genomes[i].count<10) sout<<" "<<genomes[i].count<<": "; else  sout<<genomes[i].count<<": ";
        for (int j=0; j<32; j++)
            if (tweakers64[63-j] & genomes[i].genome) sout<<"1"; else sout<<"0";
        sout<<" ";
        for (int j=32; j<64; j++)
            if (tweakers64[63-j] & genomes[i].genome) sout<<"1"; else sout<<"0";

        sout<<"  fitness: "<<genomes[i].fit<< "  Group: "<<GroupTranslate[genomes[i].group];

        sout<<"\n";
    }
    sout<<"\n";

    sout<<"Group count"<<group;
*/

    //Stuff from now on is about the output... not necessarily critical

    //OK, now we want to find the modal occurrence for each group - as they are sorted this is FIRST occurence
    QVector <int> modal(group+1);
    for (int i=1; i<=group; i++)
        if (GroupTranslate[i]!=-1)
        for (int j=0; j<genomes.count(); j++)
              if (genomes[j].group==i) {modal[i]=j; break;}


    //For each group work out and output some stats
    for (int i=group-1; i>=1; i--)
    {
        if (GroupTranslate[i]!=-1)
        {
            int minfit=999999, maxfit=-1, maxspread=-1;
            double meanfit=0, meanspread=0;
            int count=0;
            //work out summary stats

            int changes[64];
            for (int j=0; j<64; j++) changes[j]=0;
            quint64 refgenome=genomes[modal[i]].genome;
            for (int j=0; j<genomes.count(); j++)
            {
                if (genomes[j].group==i)
                {
                    count+=genomes[j].count;
                    meanfit+=genomes[j].fit*genomes[j].count;

                    if (genomes[j].fit<minfit) minfit=genomes[j].fit;
                    if (genomes[j].fit>maxfit) maxfit=genomes[j].fit;

                    quint64 g1x=refgenome ^ genomes[j].genome;
                    quint32 g1xl = quint32(g1x & ((quint64)65536*(quint64)65536-(quint64)1)); //lower 32 bits
                    int t1 = bitcounts[g1xl/(quint32)65536] +  bitcounts[g1xl & (quint32)65535];

                    quint32 g1xu = quint32(g1x / ((quint64)65536*(quint64)65536)); //upper 32 bits
                    t1+= bitcounts[g1xu/(quint32)65536] +  bitcounts[g1xu & (quint32)65535];
                    meanspread+=t1*genomes[j].count;
                    if (t1>maxspread) maxspread=t1;

                    for (int k=0; k<64; k++) if (tweakers64[k] & g1x) changes[k]+=genomes[j].count;
                }

            }
            meanfit/=count;
            meanspread/=count;

            if (count<((genomes.count()/100))) continue;

            sout<<"Group: "<<GroupTranslate[i]<<"\nGenome:   ";

            for (int k=0; k<4; k++)
            {
                for (int j=k*16; j<(k+1)*16; j++) if (tweakers64[63-j] & genomes[modal[i]].genome) sout<<"1"; else sout<<"0";
                sout<<" ";
            }
            sout<<" Dec: "<<genomes[modal[i]].genome;
            /*
            sout<<" Slots: ";
            //breed slots used
            quint32 gen2 = genomes[modal[i]].genome>>32;
            quint32 ugenecombo = (gen2>>16) ^ (gen2 & 65535);

            for (int j=0; j<16; j++) if (tweakers[j] & ugenecombo) sout<<"1"; else sout<<"0";
            */
            sout<<"\n";

            sout<<"Changes:  ";

            for (int j=0; j<64; j++)
            {
                if (changes[j]==0) changes[j]=-1;
                else
                {
                    changes[j]*=10;
                    changes[j]/=count;
                }
            }
            for (int k=0; k<4; k++)
            {
                for (int j=k*16; j<(k+1)*16; j++) if (changes[63-j]<0) sout<<" "; else sout<<changes[63-j];
                sout<<" ";
            }
            sout<<"\n";

            sout<<"Total: "<<count<<"  MinFit: "<<minfit<<"  MaxFit: "<<maxfit<<"  MeanFit: "<<meanfit<<"  MaxSpread: "<<maxspread<<"  MeanSpread: "<<meanspread<<"\n\n";
        }
    }

/*
    //Finally work out distances to each other center - and breed time overlaps

    for (int i=1; i<=group; i++)
    for (int j=i+1; j<=group; j++)
    {

        quint64 g1x = genomes[modal[i]].genome ^ genomes[modal[j]].genome; //XOR the two to compare
        quint32 g1xl = quint32(g1x & ((quint64)65536*(quint64)65536-(quint64)1)); //lower 32 bits
        int t1 = bitcounts[g1xl/(quint32)65536] +  bitcounts[g1xl & (quint32)65535];

        quint32 g1xu = quint32(g1x / ((quint64)65536*(quint64)65536)); //upper 32 bits
        t1+= bitcounts[g1xu/(quint32)65536] +  bitcounts[g1xu & (quint32)65535];

        quint32 gen2 = genomes[modal[i]].genome>>32;
        quint32 ugenecombo = (gen2>>16) ^ (gen2 & 65535);
        quint32 gen3=genomes[modal[j]].genome>>32;
        sout<<"Distance between groups "<<i<<"->"<<j<<": "<<t1<<"  Breed time overlaps: "<<bitcounts[ugenecombo & ((gen3>>16) ^ (gen3 & 65535))]<<"\n";
    }
*/


    return line;
}
