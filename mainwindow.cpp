#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "settings.h"
#include "analyser.h"
#include "fossrecwidget.h"
#include "resizecatcher.h"
#include <QTextStream>
#include <QInputDialog>
#include <QGraphicsPixmapItem>
#include <QDockWidget>
#include <QDebug>
#include <QTimer>
#include <QFileDialog>
#include <QStringList>
#include <QMessageBox>
#include <QActionGroup>
#include <QDataStream>
#include <QStringList>
#include <QFile>
#include "analysistools.h"
#include "version.h"
#include "math.h"

SimManager *TheSimManager;
MainWindow *MainWin;


#include <QThread>

class Sleeper : public QThread
{
public:
    static void usleep(unsigned long usecs){QThread::usleep(usecs);}
    static void msleep(unsigned long msecs){QThread::msleep(msecs);}
    static void sleep(unsigned long secs){QThread::sleep(secs);}
};


MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{

    a = new Analyser; // so can delete next time!
    ui->setupUi(this);
    MainWin=this;

    //Install filter to catch resize events to central widget and deliver to mainwindow (handle dock resizes)
    ResizeCatcher *rescatch = new ResizeCatcher(this);
    ui->centralWidget->installEventFilter(rescatch);

    //---- ARTS: Add Toolbar
    startButton = new QAction(QIcon(QPixmap(":/toolbar/startButton-Enabled.png")), QString("Run"), this);
    runForButton = new QAction(QIcon(QPixmap(":/toolbar/runForButton-Enabled.png")), QString("Run For..."), this);
    pauseButton = new QAction(QIcon(QPixmap(":/toolbar/pauseButton-Enabled.png")), QString("Pause"), this);
    resetButton = new QAction(QIcon(QPixmap(":/toolbar/resetButton-Enabled.png")), QString("Reset"), this);
    startButton->setEnabled(false);
    runForButton->setEnabled(false);
    pauseButton->setEnabled(false);    
    ui->toolBar->addAction(startButton);
    ui->toolBar->addAction(runForButton);
    ui->toolBar->addAction(pauseButton);
    ui->toolBar->addAction(resetButton);
    QObject::connect(startButton, SIGNAL(triggered()), this, SLOT(on_actionStart_Sim_triggered()));
    QObject::connect(runForButton, SIGNAL(triggered()), this, SLOT(on_actionRun_for_triggered()));
    QObject::connect(pauseButton, SIGNAL(triggered()), this, SLOT(on_actionPause_Sim_triggered()));
    QObject::connect(resetButton, SIGNAL(triggered()), this, SLOT(on_actionReseed_triggered()));

    //---- ARTS: Add Genome Comparison UI
    ui->genomeComparisonDock->hide();
    genoneComparison = new GenomeComparison;
    QVBoxLayout *genomeLayout = new QVBoxLayout;
    genomeLayout->addWidget(genoneComparison);
    ui->genomeComparisonContent->setLayout(genomeLayout);

    //----MDS as above for fossil record dock and report dock
    ui->fossRecDock->hide();
    FRW = new FossRecWidget();
    QVBoxLayout *frwLayout = new QVBoxLayout;
    frwLayout->addWidget(FRW);
    ui->fossRecDockContents->setLayout(frwLayout);

    ui->reportViewerDock->hide();

    viewgroup = new QActionGroup(this);
    // These actions were created via qt designer
    viewgroup->addAction(ui->actionPopulation_Count);
    viewgroup->addAction(ui->actionMean_fitness);
    viewgroup->addAction(ui->actionGenome_as_colour);
    viewgroup->addAction(ui->actionNonCoding_genome_as_colour);
    viewgroup->addAction(ui->actionGene_Frequencies_012);
    viewgroup->addAction(ui->actionSpecies);
    viewgroup->addAction(ui->actionBreed_Fails_2);
    viewgroup->addAction(ui->actionSettles);
    viewgroup->addAction(ui->actionSettle_Fails);
    QObject::connect(viewgroup, SIGNAL(triggered(QAction *)), this, SLOT(view_mode_changed(QAction *)));

    viewgroup2 = new QActionGroup(this);
    // These actions were created via qt designer
    viewgroup2->addAction(ui->actionNone);
    viewgroup2->addAction(ui->actionSorted_Summary);
    viewgroup2->addAction(ui->actionGroups);
    viewgroup2->addAction(ui->actionGroups2);
    viewgroup2->addAction(ui->actionSimple_List);

    envgroup = new QActionGroup(this);
    envgroup->addAction(ui->actionStatic);
    envgroup->addAction(ui->actionBounce);
    envgroup->addAction(ui->actionOnce);
    envgroup->addAction(ui->actionLoop);
    ui->actionLoop->setChecked(true);


    QObject::connect(viewgroup2, SIGNAL(triggered(QAction *)), this, SLOT(report_mode_changed(QAction *)));

    //create scenes, add to the GVs
    envscene = new EnvironmentScene;
    ui->GV_Environment->setScene(envscene);
    envscene->mw=this;

    popscene = new PopulationScene;
    popscene->mw=this;
    ui->GV_Population->setScene(popscene);

    //add images to the scenes    
    env_item= new QGraphicsPixmapItem();
    envscene->addItem(env_item);
    env_item->setZValue(0);

    pop_item = new QGraphicsPixmapItem();
    popscene->addItem(pop_item);

    pop_image=new QImage(gridX, gridY, QImage::Format_Indexed8);
    QVector <QRgb> clut(256);
    for (int ic=0; ic<256; ic++) clut[ic]=qRgb(ic,ic,ic);
    pop_image->setColorTable(clut);
    pop_image->fill(0);

    env_image=new QImage(gridX, gridY, QImage::Format_RGB32);
    env_image->fill(0);

    pop_image_colour=new QImage(gridX, gridY, QImage::Format_RGB32);
    env_image->fill(0);

    env_item->setPixmap(QPixmap::fromImage(*env_image));
    pop_item->setPixmap(QPixmap::fromImage(*pop_image));

    TheSimManager = new SimManager;

    FinishRun();//sets up enabling
    TheSimManager->SetupRun();
    RefreshRate=50;
    NextRefresh=0;
    Report();

    showMaximized();

    QString vstring;
    vstring.sprintf("%d.%03d",MAJORVERSION,MINORVERSION);
    this->setWindowTitle("EVOSIM v"+vstring);
    //Now set all the defaults


}

MainWindow::~MainWindow()
{
    delete ui;
    delete TheSimManager;
}

void MainWindow::on_actionReseed_triggered()
{
    TheSimManager->SetupRun();
    NextRefresh=0;
    Report();
}

void MainWindow::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
    switch (e->type()) {
    case QEvent::LanguageChange:
        ui->retranslateUi(this);
        break;
    default:
        break;
    }
}

void MainWindow::on_actionStart_Sim_triggered()
{
    if (CurrentEnvFile==-1)
    {
        QMessageBox::critical(0,"","Cannot start simulation without environment");
        if (on_actionEnvironment_Files_triggered() == false) {
            return;
        }
    }
    RunSetUp();
    while (pauseflag==false)
    {       
        Report();
        qApp->processEvents();
        if (ui->actionGo_Slow->isChecked()) Sleeper::msleep(30);
        int emode=0;
        if (ui->actionOnce->isChecked()) emode=1;
        if (ui->actionBounce->isChecked()) emode=3;
        if (ui->actionLoop->isChecked()) emode=2;
        if (TheSimManager->iterate(emode,ui->actionInterpolate->isChecked())) pauseflag=true; //returns true if reached end
        FRW->MakeRecords();
    }
    FinishRun();
}

void MainWindow::on_actionPause_Sim_triggered()
{
    pauseflag=true;
}

void MainWindow::on_actionRun_for_triggered()
{
    if (CurrentEnvFile==-1)
    {
        QMessageBox::critical(0,"","Cannot start simulation without environment");
        if (on_actionEnvironment_Files_triggered() == false) {
            return;
        }
    }

    bool ok;
    int i = QInputDialog::getInt(this, "",
                                 tr("Iterations: "), 1000, 1, 10000000, 1, &ok);
    if (!ok) return;

    RunSetUp();
    while (pauseflag==false && i>0)
    {
        Report();
        qApp->processEvents();
        int emode=0;
        if (ui->actionOnce->isChecked()) emode=1;
        if (ui->actionBounce->isChecked()) emode=3;
        if (ui->actionLoop->isChecked()) emode=2;
        if (TheSimManager->iterate(emode,ui->actionInterpolate->isChecked())) pauseflag=true;
        FRW->MakeRecords();
        i--;
    }
    FinishRun();
}

void MainWindow::on_actionRefresh_Rate_triggered()
{
    bool ok;
    int i = QInputDialog::getInt(this, "",
                                 tr("Refresh rate: "), RefreshRate, 1, 10000, 1, &ok);
    if (!ok) return;
    RefreshRate=i;
}

void MainWindow::RunSetUp()
{
    pauseflag=false;
    ui->actionStart_Sim->setEnabled(false);
    startButton->setEnabled(false);
    ui->actionRun_for->setEnabled(false);
    runForButton->setEnabled(false);
    ui->actionPause_Sim->setEnabled(true);
    pauseButton->setEnabled(true);
    ui->actionReseed->setEnabled(false);
    resetButton->setEnabled(false);
    ui->actionSettings->setEnabled(false);
    ui->actionEnvironment_Files->setEnabled(false);
    timer.restart();
    NextRefresh=RefreshRate;
}

void MainWindow::FinishRun()
{
    ui->actionStart_Sim->setEnabled(true);
    startButton->setEnabled(true);
    ui->actionRun_for->setEnabled(true);
    runForButton->setEnabled(true);
    ui->actionReseed->setEnabled(true);
    resetButton->setEnabled(true);
    ui->actionPause_Sim->setEnabled(false);
    pauseButton->setEnabled(false);
    ui->actionSettings->setEnabled(true);
    ui->actionEnvironment_Files->setEnabled(true);
    NextRefresh=0;
    Report();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    exit(0);
}

void MainWindow::Report()
{

    if (--NextRefresh>0) return;

    NextRefresh=RefreshRate;

    QString s;
    QTextStream sout(&s);

    int time=timer.elapsed();
    float atime= (float)time / (float) RefreshRate;
    timer.restart();
    double t=0;
    for (int n2=0; n2<gridX; n2++)
    for (int m2=0; m2<gridY; m2++)
        t+=totalfit[n2][m2];
    t/=(double)AliveCount;
    t/=(double)settleTolerance;
    t*=100; //now %age

    QString out;
    QTextStream o(&out);

    o<<generation; //need to use to avoid int64 issues
    ui->LabelIteration->setText(out);

    out.sprintf("%d",out.toInt()/yearsPerIteration);
    ui->LabelYears->setText(out);

    out.sprintf("%.3f",(3600000/(atime*yearsPerIteration))/1000000);
    ui->LabelMyPerHour->setText(out);

    //now back to sprintf for convenience
    if (CurrentEnvFile>=EnvFiles.count())
    out.sprintf("Finished (%d)",EnvFiles.count());
    else
    out.sprintf("%d/%d",CurrentEnvFile+1,EnvFiles.count());
    ui->LabelEnvironment->setText(out);

    out.sprintf("%.2f%%",t);
    ui->LabelFitness->setText(out);

    out.sprintf("%.2f",atime);
    ui->LabelSpeed->setText(out);

    out.sprintf("%d",AliveCount);
    ui->LabelCritters->setText(out);

    CalcSpecies();
    out="-";
    if (speciesLogging || ui->actionSpecies->isChecked())
    {
        int g5=0, g50=0;
        for (int i=0; i<oldspecieslist.count(); i++)
        {
            if (oldspecieslist[i].size>5) g5++;
            if (oldspecieslist[i].size>50) g50++;
        }
        out.sprintf("%d (>5:%d >50:%d)",oldspecieslist.count(), g5, g50);
    }
    ui->LabelSpecies->setText(out);

    RefreshReport();

    //do species stuff
    RefreshPopulations();
    RefreshEnvironment();
    FRW->RefreshMe();
    FRW->WriteFiles();

    LogSpecies();


    //reset the breedattempts and breedfails arrays
    for (int n2=0; n2<gridX; n2++)
    for (int m2=0; m2<gridY; m2++)
    {
        //breedattempts[n2][m2]=0;
        breedfails[n2][m2]=0;
        settles[n2][m2]=0;
        settlefails[n2][m2]=0;
    }
}

void MainWindow::RefreshReport()
{
    UpdateTitles();

    QTime refreshtimer;
    refreshtimer.restart();

    if (ui->actionNone->isChecked()) return;

    QString line;
    QTextStream sout(&line);
    int x=popscene->selectedx;
    int y=popscene->selectedy;
    int maxuse=maxused[x][y];
    ui->plainTextEdit->clear();
    Analyser a;

    if (ui->actionSimple_List->isChecked()) //old crappy code
    {


       for (int i=0; i<=maxuse; i++)
        {
            if (i<10) sout<<" "<<i<<": "; else  sout<<i<<": ";
            if (critters[x][y][i].age > 0 )
            {
                for (int j=0; j<32; j++)
                    if (tweakers64[63-j] & critters[x][y][i].genome) sout<<"1"; else sout<<"0";
                sout<<" ";
                for (int j=32; j<64; j++)
                    if (tweakers64[63-j] & critters[x][y][i].genome) sout<<"1"; else sout<<"0";
                sout<<"  decimal: "<<critters[x][y][i].genome<<"  fitness: "<<critters[x][y][i].fitness;
            }
            else sout<<" EMPTY";
            sout<<"\n";
        }
    }

    if (ui->actionSorted_Summary->isChecked())
    {

        for (int i=0; i<=maxuse; i++) if (critters[x][y][i].age > 0) a.AddGenome(critters[x][y][i].genome,critters[x][y][i].fitness);
        sout<<a.SortedSummary();
    }

    if (ui->actionGroups->isChecked())
    {
        for (int i=0; i<=maxuse; i++) if (critters[x][y][i].age > 0) a.AddGenome(critters[x][y][i].genome,critters[x][y][i].fitness);
        sout<<a.Groups();
    }

    if (ui->actionGroups2->isChecked())
    {
        //Code to sample all 10000 squares

        for (int n=0; n<gridX; n++)
        for (int m=0; m<gridY; m++)
        {
            if (totalfit[n][m]==0) continue;
            for (int c=0; c<slotsPerSq; c++)
            {
                if (critters[n][m][c].age>0)
                {
                    a.AddGenome_Fast(critters[n][m][c].genome);
                    break;
                }
            }
        }
        //TO DO - new report
        //sout<<a.Groups_Report();
        sout<<"Currently no code here!";

    }

    sout<<"\n\nTime for report: "<<refreshtimer.elapsed()<<"ms";

    ui->plainTextEdit->appendPlainText(line);

}

void MainWindow::UpdateTitles()
{
    if (ui->actionPopulation_Count->isChecked())
        ui->LabelVis->setText("Population Count");

    if (ui->actionMean_fitness->isChecked())
        ui->LabelVis->setText("Mean Fitness");


    if (ui->actionGenome_as_colour->isChecked())
        ui->LabelVis->setText("Coding Genome bitcount as colour (modal critter)");

    if (ui->actionNonCoding_genome_as_colour->isChecked())
        ui->LabelVis->setText("Non-Coding Genome bitcount as colour (modal critter)");

    if (ui->actionSpecies->isChecked())
        ui->LabelVis->setText("Species");

    if (ui->actionGene_Frequencies_012->isChecked())
        ui->LabelVis->setText("Frequences genes 0,1,2 (all critters in square)");

    if (ui->actionBreed_Attempts->isChecked())
        ui->LabelVis->setText("Breed Attempts");

    if (ui->actionBreed_Fails->isChecked())
        ui->LabelVis->setText("Breed Fails");

    if (ui->actionSettles->isChecked())
        ui->LabelVis->setText("Successful Settles");

    if (ui->actionSettle_Fails->isChecked())
        ui->LabelVis->setText("Breed Fails (red) and Settle Fails (green)");

    if (ui->actionBreed_Fails_2->isChecked())
        ui->LabelVis->setText("Breed Fails 2 (unused?)");
}

int MainWindow::ScaleFails(int fails, float gens)
{
    //scale colour of fail count, correcting for generations, and scaling high values to something saner
    float ffails=((float)fails)/gens;

    ffails*=100.0; // a fudge factor no less! Well it's only visualization...
    ffails=pow(ffails,0.8);

    if (ffails>255.0) ffails=255.0;
    return (int)ffails;
}

void MainWindow::RefreshPopulations()
//Refreshes of left window - also run species ident and logging
{

    //check to see what the mode is

    if (ui->actionPopulation_Count->isChecked())
    {
        //Popcount
        int bigcount=0;
        for (int n=0; n<gridX; n++)
        for (int m=0; m<gridY; m++)
        {
            int count=0;
            for (int c=0; c<slotsPerSq; c++)
            {
                if (critters[n][m][c].age) count++;
            }
            bigcount+=count;
            count*=2;
            if (count>255) count=255;
            pop_image->setPixel(n,m,count);
        }
        pop_item->setPixmap(QPixmap::fromImage(*pop_image));
    }

    if (ui->actionMean_fitness->isChecked())
    {
        //Popcount
        int multiplier=255/settleTolerance;
        for (int n=0; n<gridX; n++)
        for (int m=0; m<gridY; m++)
        {
            int count=0;
            for (int c=0; c<slotsPerSq; c++)
            {
                if (critters[n][m][c].age) count++;
            }
            if (count==0)
            pop_image->setPixel(n,m,0);
                else
            pop_image->setPixel(n,m,(totalfit[n][m] * multiplier) / count);

        }
        pop_item->setPixmap(QPixmap::fromImage(*pop_image));
    }


    if (ui->actionGenome_as_colour->isChecked())
    {
        //find modal genome in each square, convert to colour
        for (int n=0; n<gridX; n++)
        for (int m=0; m<gridY; m++)
        {
            //data structure for mode
            quint64 genomes[SLOTS_PER_GRID_SQUARE];
            int counts[SLOTS_PER_GRID_SQUARE];
            int arraypos=0; //pointer

            if (totalfit[n][m]==0) pop_image_colour->setPixel(n,m,0); //black if square is empty
            else
            {
                //for each used slot
                for (int c=0; c<maxused[n][m]; c++)
                {
                    if (critters[n][m][c].age>0)
                    {
                        //If critter is alive

                        quint64 g = critters[n][m][c].genome;

                        //find genome frequencies
                        for (int i=0; i<arraypos; i++)
                        {
                            if (genomes[i]==g) //found it
                            {
                                counts[i]++;
                                goto gotcounts;
                            }
                        }
                        //didn't find it
                        genomes[arraypos]=g;
                        counts[arraypos++]=1;
                    }
                }
                gotcounts:

                //find max frequency
                int max=-1;
                quint64 maxg=0;

                for (int i=0; i<arraypos; i++)
                    if (counts[i]>max)
                    {
                        max=counts[i];
                        maxg=genomes[i];
                    }

                //now convert first 32 bits to a colour
                // r,g,b each counts of 11,11,10 bits
                quint32 genome= (quint32)(maxg & ((quint64)65536*(quint64)65536-(quint64)1));
                quint32 b = bitcounts[genome & 2047] * 23;
                genome /=2048;
                quint32 g = bitcounts[genome & 2047] * 23;
                genome /=2048;
                quint32 r = bitcounts[genome] * 25;
                pop_image_colour->setPixel(n,m,qRgb(r, g, b));
            }

       }
        pop_item->setPixmap(QPixmap::fromImage(*pop_image_colour));
    }


    if (ui->actionSpecies->isChecked()) //do visualisation if necessary
    {
        for (int n=0; n<gridX; n++)
        for (int m=0; m<gridY; m++)
        {

            if (totalfit[n][m]==0) pop_image_colour->setPixel(n,m,0); //black if square is empty
            else
            {
                quint64 thisgenome=0;
                for (int c=0; c<slotsPerSq; c++)
                {
                    if (critters[n][m][c].age>0)
                    {
                        thisgenome=critters[n][m][c].genome;
                        break;
                    }
                }

                int species = a->SpeciesIndex(thisgenome);

                if (species==-1)
                    pop_image_colour->setPixel(n,m,qRgb(255,255,255));
                else
                    pop_image_colour->setPixel(n,m,species_colours[species % 65536]);
            }
        }
        pop_item->setPixmap(QPixmap::fromImage(*pop_image_colour));
    }

    if (ui->actionNonCoding_genome_as_colour->isChecked())
    {
        //find modal genome in each square, convert non-coding to colour
        for (int n=0; n<gridX; n++)
        for (int m=0; m<gridY; m++)
        {
            //data structure for mode
            quint64 genomes[SLOTS_PER_GRID_SQUARE];
            int counts[SLOTS_PER_GRID_SQUARE];
            int arraypos=0; //pointer

            if (totalfit[n][m]==0) pop_image_colour->setPixel(n,m,0); //black if square is empty
            else
            {
                //for each used slot
                for (int c=0; c<maxused[n][m]; c++)
                {
                    if (critters[n][m][c].age>0)
                    {
                        //If critter is alive

                        quint64 g = critters[n][m][c].genome;

                        //find genome frequencies
                        for (int i=0; i<arraypos; i++)
                        {
                            if (genomes[i]==g) //found it
                            {
                                counts[i]++;
                                goto gotcounts2;
                            }
                        }
                        //didn't find it
                        genomes[arraypos]=g;
                        counts[arraypos++]=1;
                    }
                }
                gotcounts2:

                //find max frequency
                int max=-1;
                quint64 maxg=0;

                for (int i=0; i<arraypos; i++)
                    if (counts[i]>max)
                    {
                        max=counts[i];
                        maxg=genomes[i];
                    }

                //now convert first 32 bits to a colour
                // r,g,b each counts of 11,11,10 bits
                quint32 genome= (quint32)(maxg / ((quint64)65536*(quint64)65536));
                quint32 b = bitcounts[genome & 2047] * 23;
                genome /=2048;
                quint32 g = bitcounts[genome & 2047] * 23;
                genome /=2048;
                quint32 r = bitcounts[genome] * 25;
                pop_image_colour->setPixel(n,m,qRgb(r, g, b));
            }
       }
        pop_item->setPixmap(QPixmap::fromImage(*pop_image_colour));
    }

    if (ui->actionGene_Frequencies_012->isChecked())
    {
        //Popcount
        for (int n=0; n<gridX; n++)
        for (int m=0; m<gridY; m++)
        {
            int count=0;
            int gen0tot=0;
            int gen1tot=0;
            int gen2tot=0;
            for (int c=0; c<slotsPerSq; c++)
            {
                if (critters[n][m][c].age)
                {
                    count++;
                    if (critters[n][m][c].genome & 1) gen0tot++;
                    if (critters[n][m][c].genome & 2) gen1tot++;
                    if (critters[n][m][c].genome & 4) gen2tot++;
                }
            }
            if (count==0) pop_image_colour->setPixel(n,m,qRgb(0, 0, 0));
            else
            {
                quint32 r = gen0tot *256 / count;
                quint32 g = gen1tot *256 / count;
                quint32 b = gen2tot *256 / count;
                pop_image_colour->setPixel(n,m,qRgb(r, g, b));
            }
          }
        pop_item->setPixmap(QPixmap::fromImage(*pop_image_colour));
    }

    if (ui->actionBreed_Attempts->isChecked())
    {
        //Popcount
        for (int n=0; n<gridX; n++)
        for (int m=0; m<gridY; m++)
        {
            int value=(breedattempts[n][m]*10)/RefreshRate;
            if (value>255) value=255;
            pop_image->setPixel(n,m,value);
        }
        pop_item->setPixmap(QPixmap::fromImage(*pop_image));
    }

    if (ui->actionBreed_Fails->isChecked())
    {
        //Popcount
        for (int n=0; n<gridX; n++)
        for (int m=0; m<gridY; m++)
        {
            if (breedattempts[n][m]==0) pop_image->setPixel(n,m,0);
            else
            {
                int value=(breedfails[n][m]*255)/breedattempts[n][m];
                pop_image->setPixel(n,m,value);
            }
        }
        pop_item->setPixmap(QPixmap::fromImage(*pop_image));
    }

    if (ui->actionSettles->isChecked())
    {
        //Popcount
        for (int n=0; n<gridX; n++)
        for (int m=0; m<gridY; m++)
        {
            int value=(settles[n][m]*10)/RefreshRate;
            if (value>255) value=255;
            pop_image->setPixel(n,m,value);
        }
        pop_item->setPixmap(QPixmap::fromImage(*pop_image));
    }

    if (ui->actionSettle_Fails->isChecked())
    //this now combines breed fails (red) and settle fails (green)
    {
        //work out max and ratios
        /*int maxbf=1;
        int maxsf=1;
        for (int n=0; n<gridX; n++)
        for (int m=0; m<gridY; m++)
        {
            if (settlefails[n][m]>maxsf) maxsf=settlefails[n][m];
            if (breedfails[n][m]>maxsf) maxbf=breedfails[n][m];
        }
        float bf_mult=255.0 / (float)maxbf;
        float sf_mult=255.0 / (float)maxsf;
        qDebug()<<bf_mult<<sf_mult;
        bf_mult=1.0;
        sf_mult=1.0;
        */
        //work out average per generation
        float gens=generation-lastReport;


        //Make image
        for (int n=0; n<gridX; n++)
        for (int m=0; m<gridY; m++)
        {
            int r=ScaleFails(breedfails[n][m],gens);
            int g=ScaleFails(settlefails[n][m],gens);
            pop_image_colour->setPixel(n,m,qRgb(r, g, 0));
        }
        pop_item->setPixmap(QPixmap::fromImage(*pop_image_colour));
    }

    if (ui->actionBreed_Fails_2->isChecked())
    {
        //Popcount
        for (int n=0; n<gridX; n++)
        for (int m=0; m<gridY; m++)
        {
            if (breedfails[n][m]==0) pop_image->setPixel(n,m,0);
            else
            {
                int value=(breedfails[n][m]);
                if (value>255) value=255;
                pop_image->setPixel(n,m,value);
            }
        }
        pop_item->setPixmap(QPixmap::fromImage(*pop_image));
    }

    lastReport=generation;
}

void MainWindow::RefreshEnvironment()
{


    for (int n=0; n<gridX; n++)
    for (int m=0; m<gridY; m++)
        env_image->setPixel(n,m,qRgb(environment[n][m][0], environment[n][m][1], environment[n][m][2]));

    env_item->setPixmap(QPixmap::fromImage(*env_image));

    //Draw on fossil records
    envscene->DrawLocations(FRW->FossilRecords,ui->actionShow_positions->isChecked());
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    //force a rescale of the graphic view
    Resize();
}

void MainWindow::Resize()
{
    ui->GV_Population->fitInView(pop_item,Qt::KeepAspectRatio);
    ui->GV_Environment->fitInView(env_item,Qt::KeepAspectRatio);
}

void MainWindow::view_mode_changed(QAction *temp2)
{
    UpdateTitles();
    RefreshPopulations();
}

void MainWindow::report_mode_changed(QAction *temp2)
{
    RefreshReport();
}

void MainWindow::ResetSquare(int n, int m)
{
    //grid expanded - make sure everything is zeroed in new slots
    for (int c=0; c<slotsPerSq; c++) critters[n][m][c].age=0;

    totalfit[n][m]=0;

    breedattempts[n][m]=0;
    breedfails[n][m]=0;
    settles[n][m]=0;
    settlefails[n][m]=0;

}


void MainWindow::ResizeImageObjects()
{
    delete pop_image;
    delete env_image;
    delete pop_image_colour;
    pop_image =new QImage(gridX, gridY, QImage::Format_Indexed8);
    QVector <QRgb> clut(256);
    for (int ic=0; ic<256; ic++) clut[ic]=qRgb(ic,ic,ic);
    pop_image->setColorTable(clut);

    env_image=new QImage(gridX, gridY, QImage::Format_RGB32);

    pop_image_colour=new QImage(gridX, gridY, QImage::Format_RGB32);
}
void MainWindow::on_actionSettings_triggered()
{
    //AutoMarkers options tab
    //Something like:

    int oldrows, oldcols;
    oldrows=gridX; oldcols=gridY;
    SettingsImpl Dialog;
    Dialog.exec();


    if (Dialog.RedoImages)
    {

        //check that the maxused's are in the new range
         for (int n=0; n<gridX; n++)
         for (int m=0; m<gridY; m++)
             if (maxused[n][m]>=slotsPerSq) maxused[n][m]=slotsPerSq-1;

         //If either rows or cols are bigger - make sure age is set to 0 in all critters in new bit!
        if (gridX>oldrows)
        {
            for (int n=oldrows; n<gridX; n++) for (int m=0; m<gridY; m++)
                ResetSquare(n,m);
        }
        if (gridY>oldcols)
        {
            for (int n=0; n<gridX; n++) for (int m=oldcols; m<gridY; m++)
                ResetSquare(n,m);
        }

        ResizeImageObjects();

        RefreshPopulations();
        RefreshEnvironment();
        Resize();
    }
}


void MainWindow::on_actionMisc_triggered()
{
    TheSimManager->testcode();
}

void MainWindow::on_actionCount_Peaks_triggered()
{
    HandleAnalysisTool(4);
}

bool  MainWindow::on_actionEnvironment_Files_triggered()
{
    //Select files
    QStringList files = QFileDialog::getOpenFileNames(
                            this,
                            "Select one or more image files to load in simulation environment...",
                            "",
                            "Images (*.png *.bmp)");

    if (files.length()==0) return false;
    EnvFiles = files;
    CurrentEnvFile=0;
    int emode=0;
    if (ui->actionOnce->isChecked()) emode=1;
    if (ui->actionBounce->isChecked()) emode=3;
    if (ui->actionLoop->isChecked()) emode=2;
    TheSimManager->loadEnvironmentFromFile(emode);
    RefreshEnvironment();
    return true;
}

void MainWindow::on_actionChoose_Log_Directory_triggered()
{
    QString dirname = QFileDialog::getExistingDirectory(this,"Select directory to log fossil record to");


    if (dirname.length()==0) return;
    FRW->LogDirectory=dirname;
    FRW->LogDirectoryChosen=true;
    FRW->HideWarnLabel();

}

void MainWindow::on_actionSave_triggered()
{
    QString filename = QFileDialog::getSaveFileName(
                            this,
                            "Save file",
                            "",
                            "EVOSIM files (*.evosim)");

    if (filename.length()==0) return;

    //Otherwise - serialise all my crap
    QFile outfile(filename);
    outfile.open(QIODevice::WriteOnly);

    QDataStream out(&outfile);


    out<<QString("EVOSIM file");
    out<<(int)VERSION;
    out<<gridX;
    out<<gridY;
    out<<slotsPerSq;
    out<<startAge;
    out<<target;
    out<<settleTolerance;
    out<<dispersal;
    out<<food;
    out<<breedThreshold;
    out<<breedCost;
    out<<maxDiff;
    out<<mutate;
    out<<envchangerate;
    out<<CurrentEnvFile;
    out<<EnvChangeCounter;
    out<<EnvChangeForward;
    out<<AliveCount;
    out<<RefreshRate;
    out<<generation;

    //action group settings
    int emode=0;
    if (ui->actionOnce->isChecked()) emode=1;
    if (ui->actionBounce->isChecked()) emode=3;
    if (ui->actionLoop->isChecked()) emode=2;
    out<<emode;

    int vmode=0;
    if (ui->actionPopulation_Count->isChecked()) vmode=0;
    if (ui->actionMean_fitness->isChecked()) vmode=1;
    if (ui->actionGenome_as_colour->isChecked()) vmode=2;
    if (ui->actionNonCoding_genome_as_colour->isChecked()) vmode=3;
    if (ui->actionGene_Frequencies_012->isChecked()) vmode=4;
    if (ui->actionBreed_Attempts->isChecked()) vmode=5;
    if (ui->actionBreed_Fails->isChecked()) vmode=6;
    if (ui->actionSettles->isChecked()) vmode=7;
    if (ui->actionSettle_Fails->isChecked()) vmode=8;
    if (ui->actionBreed_Fails_2->isChecked()) vmode=9;
    if (ui->actionSpecies->isChecked()) vmode=10;

    out<<vmode;

    int rmode=0;
    if (ui->actionNone->isChecked()) rmode=0;
    if (ui->actionSorted_Summary->isChecked()) rmode=1;
    if (ui->actionGroups->isChecked()) rmode=2;
    if (ui->actionGroups2->isChecked()) rmode=3;
    if (ui->actionSimple_List->isChecked()) rmode=4;
    out<<rmode;

    //file list
    out<<EnvFiles.count();
    for (int i=0; i<EnvFiles.count(); i++)
        out<<EnvFiles[i];

    //now the big arrays

    for (int i=0; i<gridX; i++)
    for (int j=0; j<gridY; j++)
    for (int k=0; k<slotsPerSq; k++)
    {
        if ((critters[i][j][k]).age==0) //its dead
            out<<(int)0;
        else
        {
            out<<critters[i][j][k].age;
            out<<critters[i][j][k].genome;
            out<<critters[i][j][k].ugenecombo;
            out<<critters[i][j][k].fitness;
            out<<critters[i][j][k].energy;
            out<<critters[i][j][k].xpos;
            out<<critters[i][j][k].ypos;
            out<<critters[i][j][k].zpos;
        }
    }

    for (int i=0; i<gridX; i++)
    for (int j=0; j<gridY; j++)
    {
        out<<environment[i][j][0];
        out<<environment[i][j][1];
        out<<environment[i][j][2];
    }

    for (int i=0; i<gridX; i++)
    for (int j=0; j<gridY; j++)
    {
        out<<totalfit[i][j];
    }

    for (int i=0; i<256; i++)
    for (int j=0; j<3; j++)
    {
        out<<xormasks[i][j];
    }

    //---- ARTS Genome Comparison Saving
    out<<genoneComparison->saveComparison();

    //and fossil record stuff
    FRW->WriteFiles(); //make sure all is flushed
    out<<FRW->SaveState();

    //New Year Per Iteration variable
    out<<yearsPerIteration;

    //Some more stuff that was missing from first version
    out<<ui->actionShow_positions->isChecked();

    for (int i=0; i<gridX; i++)
    for (int j=0; j<gridY; j++)
    {
        out<<breedattempts[i][j];
        out<<breedfails[i][j];
        out<<settles[i][j];
        out<<settlefails[i][j];
        out<<maxused[i][j];
    }

    //And some window state stuff
    out<<saveState(); //window state
    out<<ui->actionFossil_Record->isChecked();
    out<<ui->actionReport_Viewer->isChecked();
    out<<ui->actionGenomeComparison->isChecked();

    //interpolate environment stuff
    out<<ui->actionInterpolate->isChecked();
    for (int i=0; i<gridX; i++)
    for (int j=0; j<gridY; j++)
    {
        out<<environmentlast[i][j][0];
        out<<environmentlast[i][j][1];
        out<<environmentlast[i][j][2];
    }
    for (int i=0; i<gridX; i++)
    for (int j=0; j<gridY; j++)
    {
        out<<environmentnext[i][j][0];
        out<<environmentnext[i][j][1];
        out<<environmentnext[i][j][2];
    }

    out<<speciesSamples;
    out<<speciesSensitivity;
    out<<timeSliceConnect;
    out<<speciesLogging;
    out<<speciesLoggingToFile;
    out<<SpeciesLoggingFile;


    //now the species archive
    out<<oldspecieslist.count();
    for (int j=0; j<oldspecieslist.count(); j++)
    {
         out<<oldspecieslist[j].ID;
         out<<oldspecieslist[j].type;
         out<<oldspecieslist[j].origintime;
         out<<oldspecieslist[j].parent;
         out<<oldspecieslist[j].size;
         out<<oldspecieslist[j].internalID;
    }

    out<<archivedspecieslists.count();
    for (int i=0; i<archivedspecieslists.count(); i++)
    {
        out<<archivedspecieslists[i].count();
        for (int j=0; j<archivedspecieslists[i].count(); j++)
        {
             out<<archivedspecieslists[i][j].ID;
             out<<archivedspecieslists[i][j].type;
             out<<archivedspecieslists[i][j].origintime;
             out<<archivedspecieslists[i][j].parent;
             out<<archivedspecieslists[i][j].size;
             out<<archivedspecieslists[i][j].internalID;
        }
    }
    out<<nextspeciesid;
    out<<lastSpeciesCalc;

    //now random number array
    for (int i=0; i<65536; i++)
        out<<randoms[i];

    out<<recalcFitness; //extra new parameter

    outfile.close();
}

void MainWindow::on_actionLoad_triggered()
{

    QString filename = QFileDialog::getOpenFileName(
                            this,
                            "Save file",
                            "",
                            "EVOSIM files (*.evosim)");

    if (filename.length()==0) return;

    if (pauseflag==false) pauseflag=true;


    //Otherwise - serialise all my crap
    QFile infile(filename);
    infile.open(QIODevice::ReadOnly);

    QDataStream in(&infile);

    QString temp;
    in>>temp;
    if (temp!="EVOSIM file")
    {QMessageBox::warning(this,"","Not an EVOSIM file"); return;}

    int version;
    in>>version;
    if (version>VERSION)
    {QMessageBox::warning(this,"","Version too high - will try to read, but may go horribly wrong");}

    in>>gridX;
    in>>gridY;
    in>>slotsPerSq;
    in>>startAge;
    in>>target;
    in>>settleTolerance;
    in>>dispersal;
    in>>food;
    in>>breedThreshold;
    in>>breedCost;
    in>>maxDiff;
    in>>mutate;
    in>>envchangerate;
    in>>CurrentEnvFile;
    in>>EnvChangeCounter;
    in>>EnvChangeForward;
    in>>AliveCount;
    in>>RefreshRate;
    in>>generation;

    int emode;
    in>>emode;
    if (emode==0) ui->actionStatic->setChecked(true);
    if (emode==1) ui->actionOnce->setChecked(true);
    if (emode==3) ui->actionBounce->setChecked(true);
    if (emode==2) ui->actionLoop->setChecked(true);

    int vmode;
    in>>vmode;
    if (vmode==0) ui->actionPopulation_Count->setChecked(true);
    if (vmode==1) ui->actionMean_fitness->setChecked(true);
    if (vmode==2) ui->actionGenome_as_colour->setChecked(true);
    if (vmode==3) ui->actionNonCoding_genome_as_colour->setChecked(true);
    if (vmode==4) ui->actionGene_Frequencies_012->setChecked(true);
    if (vmode==5) ui->actionBreed_Attempts->setChecked(true);
    if (vmode==6) ui->actionBreed_Fails->setChecked(true);
    if (vmode==7) ui->actionSettles->setChecked(true);
    if (vmode==8) ui->actionSettle_Fails->setChecked(true);
    if (vmode==9) ui->actionBreed_Fails_2->setChecked(true);
    if (vmode==10) ui->actionSpecies->setChecked(true);

    int rmode;
    in>>rmode;
    if (rmode==0) ui->actionNone->setChecked(true);
    if (rmode==1) ui->actionSorted_Summary->setChecked(true);
    if (rmode==2) ui->actionGroups->setChecked(true);
    if (rmode==3) ui->actionGroups2->setChecked(true);
    if (rmode==4) ui->actionSimple_List->setChecked(true);

    int count;

    //file list
    in>>count;
    EnvFiles.clear();

    for (int i=0; i<count; i++)
    {
        QString t;
        in>>t;
        EnvFiles.append(t);
    }

    //now the big arrays

    for (int i=0; i<gridX; i++)
    for (int j=0; j<gridY; j++)
    for (int k=0; k<slotsPerSq; k++)
    {
        in>>critters[i][j][k].age;
        if (critters[i][j][k].age>0)
        {
            in>>critters[i][j][k].genome;
            in>>critters[i][j][k].ugenecombo;
            in>>critters[i][j][k].fitness;
            in>>critters[i][j][k].energy;
            in>>critters[i][j][k].xpos;
            in>>critters[i][j][k].ypos;
            in>>critters[i][j][k].zpos;
        }
    }

    for (int i=0; i<gridX; i++)
    for (int j=0; j<gridY; j++)
    {
        in>>environment[i][j][0];
        in>>environment[i][j][1];
        in>>environment[i][j][2];
    }

    for (int i=0; i<gridX; i++)
    for (int j=0; j<gridY; j++)
    {
        in>>totalfit[i][j];
    }

    for (int i=0; i<256; i++)
    for (int j=0; j<3; j++)
    {
        in>>xormasks[i][j];
    }

    //---- ARTS Genome Comparison Loading
    QByteArray Temp;
    in>>Temp;
    genoneComparison->loadComparison(Temp);

    //and fossil record stuff
    in>>Temp;
    FRW->RestoreState(Temp);

    //New Year Per Iteration variable
    in>>yearsPerIteration;

    //Some more stuff that was missing from first version
    bool btemp;
    in>>btemp;
    ui->actionShow_positions->setChecked(btemp);

    for (int i=0; i<gridX; i++)
    for (int j=0; j<gridY; j++)
    {
        in>>breedattempts[i][j];
        in>>breedfails[i][j];
        in>>settles[i][j];
        in>>settlefails[i][j];
        in>>maxused[i][j];
    }

    in>>Temp;
    restoreState(Temp); //window state

    in>>btemp; ui->actionFossil_Record->setChecked(btemp);
    in>>btemp; ui->actionReport_Viewer->setChecked(btemp);
    in>>btemp; ui->actionGenomeComparison->setChecked(btemp);

    //interpolate environment stuff
    in>>btemp;
    ui->actionInterpolate->setChecked(btemp);

    for (int i=0; i<gridX; i++)
    for (int j=0; j<gridY; j++)
    {
        in>>environmentlast[i][j][0];
        in>>environmentlast[i][j][1];
        in>>environmentlast[i][j][2];
    }

    for (int i=0; i<gridX; i++)
    for (int j=0; j<gridY; j++)
    {
        in>>environmentnext[i][j][0];
        in>>environmentnext[i][j][1];
        in>>environmentnext[i][j][2];
    }

    if (!(in.atEnd())) in>>speciesSamples;
    if (!(in.atEnd())) in>>speciesSensitivity;
    if (!(in.atEnd())) in>>timeSliceConnect;
    if (!(in.atEnd())) in>>speciesLogging;
    if (!(in.atEnd())) in>>speciesLoggingToFile;
    if (!(in.atEnd())) in>>SpeciesLoggingFile;

    if (speciesLogging) ui->actionTracking->setChecked(true); else ui->actionTracking->setChecked(false);
    if (speciesLoggingToFile)  {ui->actionLogging->setChecked(true); ui->actionTracking->setEnabled(false);}  else {ui->actionLogging->setChecked(false); ui->actionTracking->setEnabled(true);}
    if (SpeciesLoggingFile!="") ui->actionLogging->setEnabled(true); else ui->actionLogging->setEnabled(false);


    //now the species archive
    archivedspecieslists.clear();
    oldspecieslist.clear();

    if (!(in.atEnd()))
    {
        int temp;
        in>>temp; //oldspecieslist.count();
        for (int j=0; j<temp; j++)
        {
             species s;
             in>>s.ID;
             in>>s.type;
             in>>s.origintime;
             in>>s.parent;
             in>>s.size;
             in>>s.internalID;
             oldspecieslist.append(s);
        }

        in>>temp; //archivedspecieslists.count();

        for (int i=0; i<temp; i++)
        {
            int temp2;
            in>>temp2; //archivedspecieslists.count();
            QList<species> ql;
            for (int j=0; j<temp2; j++)
            {
                species s;
                in>>s.ID;
                in>>s.type;
                in>>s.origintime;
                in>>s.parent;
                in>>s.size;
                in>>s.internalID;
                ql.append(s);
            }
            archivedspecieslists.append(ql);
        }
        in>>nextspeciesid;
        in>>lastSpeciesCalc; //actually no - if we import this it will assume an 'a' object exists.
        //bodge
        lastSpeciesCalc--;
    }

    //now random array
    if (!(in.atEnd()))
        for (int i=0; i<65536; i++)
            in>>randoms[i];

    if (!(in.atEnd()))
        in>>recalcFitness;

    infile.close();
    NextRefresh=0;
    ResizeImageObjects();
    Report();
    Resize();
}

//---- ARTS: Genome Comparison UI ----
bool MainWindow::genomeComparisonAdd()
{
    int x=popscene->selectedx;
    int y=popscene->selectedy;

    //---- Get genome colour
    if (totalfit[x][y]!=0) {
        for (int c=0; c<slotsPerSq; c++)
        {
            if (critters[x][y][c].age>0){
                genoneComparison->addGenomeCritter(critters[x][y][c],environment[x][y]);
                return true;
            }
        }       
    }
    return false;
}

void MainWindow::on_actionAdd_Regular_triggered()
{
    int count;
    bool ok;
    count=QInputDialog::getInt(this,"","Grid Density?",2,2,10,1,&ok);
    if (!ok) return;

    for (int x=0; x<count; x++)
    for (int y=0; y<count; y++)
    {
        int rx=(int)((((float)gridX/(float)count)/(float)2 + (float)gridX/(float)count * (float)x)+.5);
        int ry=(int)((((float)gridY/(float)count)/(float)2 + (float)gridY/(float)count * (float)y)+.5);
        FRW->NewItem(rx,ry,10);
    }
}

void MainWindow::on_actionAdd_Random_triggered()
{
    int count;
    bool ok;
    count=QInputDialog::getInt(this,"","Number of records to add?",1,1,100,1,&ok);
    if (!ok) return;
    for (int i=0; i<count; i++)
    {
        int rx=TheSimManager->portable_rand() % gridX;
        int ry=TheSimManager->portable_rand() % gridY;
        FRW->NewItem(rx,ry,10);
    }
}

void MainWindow::on_actionSet_Active_triggered()
{
    FRW->SelectedActive(true);
}

void MainWindow::on_actionSet_Inactive_triggered()
{
    FRW->SelectedActive(false);
}

void MainWindow::on_actionSet_Sparsity_triggered()
{

    bool ok;

    int value=QInputDialog::getInt(this,"","Sparsity",10,1,100000,1,&ok);
    if (!ok) return;

    FRW->SelectedSparse(value);

}

void MainWindow::on_actionShow_positions_triggered()
{
    RefreshEnvironment();
}

void MainWindow::on_actionTracking_triggered()
{
    speciesLogging=ui->actionTracking->isChecked();
}

void MainWindow::on_actionLogging_triggered()
{
    speciesLoggingToFile=ui->actionLogging->isChecked();
    if (speciesLoggingToFile)
       { ui->actionTracking->setChecked(true); ui->actionTracking->setEnabled(false); speciesLogging=true; }
    else ui->actionTracking->setEnabled(true);
}

void MainWindow::on_actionSet_Logging_File_triggered()
{
    QString filename = QFileDialog::getSaveFileName(this,"Select file to log fossil record to","","*.csv");
    if (filename.length()==0) return;

    SpeciesLoggingFile=filename;
    ui->actionLogging->setEnabled(true);
}




void MainWindow::on_actionGenerate_Tree_from_Log_File_triggered()
{
    HandleAnalysisTool(0);
}


void MainWindow::on_actionRates_of_Change_triggered()
{
    HandleAnalysisTool(1);
}

void MainWindow::on_actionStasis_triggered()
{
    HandleAnalysisTool(3);
}

void MainWindow::on_actionExtinction_and_Origination_Data_triggered()
{
    HandleAnalysisTool(2);
}

void MainWindow::CalcSpecies()
{
    if (generation!=lastSpeciesCalc)
    {
        delete a;  //replace old analyser object with new
        a=new Analyser;

        if (ui->actionSpecies->isChecked() || speciesLogging) //do species calcs here even if not showing species - unless turned off in settings
        {
            //set up species ID

            for (int n=0; n<gridX; n++)
            for (int m=0; m<gridY; m++)
            {
                if (totalfit[n][m]==0) continue;
                int found=0;
                for (int c=0; c<slotsPerSq; c++)
                {
                    if (critters[n][m][c].age>0)
                    {
                        a->AddGenome_Fast(critters[n][m][c].genome);
                        if ((++found)>=speciesSamples) break; //limit number sampled
                    }
                }
            }

            a->Groups_With_History_Modal();
            lastSpeciesCalc=generation;
        }

    }
}

void MainWindow::LogSpecies()
{
    if (speciesLoggingToFile==false) return;

    //log em!
    QFile outputfile(SpeciesLoggingFile);

    if (!(outputfile.exists()))
    {
        outputfile.open(QIODevice::WriteOnly);
        QTextStream out(&outputfile);

        out<<"Time,Species_ID,Species_origin_time,Species_parent_ID,Species_current_size,Species_current_genom,";

        //for (int j=0; j<63; j++) out<<j<<",";
        //out<<"63\n";
        outputfile.close();
    }

    outputfile.open(QIODevice::Append);
    QTextStream out(&outputfile);

    for (int i=0; i<oldspecieslist.count(); i++)
    {
        out<<generation;
        out<<","<<(oldspecieslist[i].ID);
        out<<","<<oldspecieslist[i].origintime;
        out<<","<<oldspecieslist[i].parent;
        out<<","<<oldspecieslist[i].size;
        out<<","<<oldspecieslist[i].type<<"\n";
        /*
             ",";
        for (int j=0; j<63; j++)
           if (tweakers64[63-j] & oldspecieslist[i].type) out<<"1,"; else out<<"0,";
        if (tweakers64[0] & oldspecieslist[i].type) out<<"1\n"; else out<<"0\n";
        */
    }
    outputfile.close();
}

void MainWindow::setStatusBarText(QString text)
{
    ui->statusBar->showMessage(text);
}

void MainWindow::on_actionLoad_Random_Numbers_triggered()
{
    //Select files
    QString file = QFileDialog::getOpenFileName(
                            this,
                            "Select random number file",
                            "",
                            "*.*");

    if (file.length()==0) return;

    int seedoffset;
    seedoffset=QInputDialog::getInt(this,"Seed value","Byte offset to start reading from (will read 65536 bytes)");

    //now read in values to array
    QFile rfile(file);
    rfile.open(QIODevice::ReadOnly);

    rfile.seek(seedoffset);

    int i=rfile.read((char *)randoms,65536);
    if (i!=65536) QMessageBox::warning(this,"Oops","Failed to read 65536 bytes from file - random numbers may be compromised - try again or restart program");
    else QMessageBox::information(this,"Success","New random numbers read successfully");
}

void MainWindow::on_SelectLogFile_pressed()
{
    QString filename = QFileDialog::getOpenFileName(this,"Select log file","","*.csv");
    if (filename.length()==0) return;

    ui->LogFile->setText(filename);
}

void MainWindow::on_SelectOutputFile_pressed()
{
    QString filename = QFileDialog::getSaveFileName(this,"Select log file","","*.*");
    if (filename.length()==0) return;

    ui->OutputFile->setText(filename);
}

void MainWindow::HandleAnalysisTool(int code)
{
    //do filenames
    //Is there a valid input file?

    AnalysisTools a;
    QString OutputString;

    if (code==4)
        OutputString = a.CountPeaks(ui->PeaksRed->value(),ui->PeaksGreen->value(),ui->PeaksBlue->value());
    else
    {
        QFile f(ui->LogFile->text());
        if (!(f.exists()))
        {
            QMessageBox::warning(this,"Error","No valid input file set");
            return;
        }

        if (code==0)  OutputString = a.GenerateTree(ui->LogFile->text());
        if (code==1)  OutputString = a.SpeciesRatesOfChange(ui->LogFile->text());
        if (code==2)  OutputString = a.ExtinctOrigin(ui->LogFile->text());
        if (code==3)  OutputString = a.Stasis(ui->LogFile->text(),ui->StasisBins->value(),((float)ui->StasisPercentile->value())/100.0,ui->StasisQualify->value());
    }

    //write result to screen
    ui->plainTextEdit->clear();
    ui->plainTextEdit->appendPlainText(OutputString);

    //and attempt to write to file
    if (ui->OutputFile->text().length()>1) //i.e. if not blank
    {
        QFile o(ui->OutputFile->text());
        if (!o.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            QMessageBox::warning(this,"Error","Could not open output file for writing");
            return;
        }

        QTextStream out(&o);
        out<<OutputString;
        o.close();
    }
}
