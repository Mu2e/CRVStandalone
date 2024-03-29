#include "WLSEventAction.hh"

#include "G4Event.hh"
#include "G4EventManager.hh"
#include "G4CerenkovNew.hh"
#include "WLSSteppingAction.hh"
#include "WLSStackingAction.hh"
#include "WLSDetectorConstruction.hh"

#include "Randomize.hh"
#include <TMath.h>
#include <TFitResult.h>
#include <TFitResultPtr.h>
#include <TF1.h>
#include <TH1D.h>
#include <TH2D.h>
#include <TH3D.h>
#include <TNtuple.h>
#include <TFile.h>

#include <TStyle.h>
#include <TText.h>
#include <TGraph.h>
#include <TMarker.h>
#include <TGaxis.h>
#include <TCanvas.h>
#include <TPaveStats.h>

#include "MakeCrvPhotons.hh"
#include "MakeCrvSiPMCharges.hh"
#include "MakeCrvWaveforms.hh"
#include "MakeCrvDigis.hh"
#include "MakeCrvRecoPulses.hh"

#include <stdexcept>
#include <boost/shared_ptr.hpp>

#include "CLHEP/Random/Randomize.h"

WLSEventAction* WLSEventAction::_fgInstance = NULL;

WLSEventAction::WLSEventAction(WLSSteppingAction::simulationMode mode, const std::string &singlePEWaveformFilename, const std::string &photonMapFilename,  
                               int numberOfPhotons, int simType, unsigned int minBin, bool verbose) : 
                                                                                         _mode(mode), 
                                                                                         _numberOfPhotons(numberOfPhotons), 
                                                                                         _simType(simType), 
                                                                                         _minBin(minBin), 
                                                                                         _currentBin(minBin), 
                                                                                         _singlePEWaveformFilename(singlePEWaveformFilename), 
                                                                                         _photonMapFilename(photonMapFilename), 
                                                                                         _verbose(verbose)
                               //numberOfPhotons, simType, minBin, currentBin, verbose is only needed for simulationMode::CreateLookupTables
{
  _fgInstance = this;

  if(_mode==WLSSteppingAction::UseGeantOnly || _mode==WLSSteppingAction::UseGeantAndLookupTables)
  {
    for(int SiPM=0; SiPM<4; SiPM++)
    {
      std::stringstream s0, s1, title;
      s0<<"Photons_Geant_SiPM_"<<SiPM;
      s1<<"Photons_LookupTable_SiPM_"<<SiPM;
      title<<"Fiber: "<<SiPM/2<<",  Side: "<<SiPM%2;
      _histP[0][SiPM] = new TH1D(s0.str().c_str(),title.str().c_str(),1000,0,1000);
      _histP[1][SiPM] = new TH1D(s1.str().c_str(),title.str().c_str(),1000,0,1000);
      _histP[0][SiPM]->GetXaxis()->SetTitle("Photons");
      _histP[0][SiPM]->SetLineColor(1);
      _histP[1][SiPM]->GetXaxis()->SetTitle("Photons");
      _histP[1][SiPM]->SetLineColor(2);
    }

    for(int SiPM=0; SiPM<4; SiPM++)
    {
      std::stringstream s0, s1, title;
      s0<<"ArrivalTimes_Geant_SiPM_"<<SiPM;
      s1<<"ArrivalTimes_LookupTable_SiPM_"<<SiPM;
      title<<"Fiber: "<<SiPM/2<<",  Side: "<<SiPM%2;
      _histT[0][SiPM] = new TH1D(s0.str().c_str(),title.str().c_str(),250,0,250);
      _histT[1][SiPM] = new TH1D(s1.str().c_str(),title.str().c_str(),250,0,250);
      _histT[0][SiPM]->GetXaxis()->SetTitle("t [ns]");
      _histT[0][SiPM]->SetLineColor(1);
      _histT[1][SiPM]->GetXaxis()->SetTitle("t [ns]");
      _histT[1][SiPM]->SetLineColor(2);
    }

    for(int SiPM=0; SiPM<4; SiPM++)
    {
      std::stringstream s0, title;
      s0<<"PEs_SiPM_"<<SiPM;
      title<<"Fiber: "<<SiPM/2<<",  Side: "<<SiPM%2;
      _histPE[SiPM] = new TH1D(s0.str().c_str(),title.str().c_str(),500,0,500);
      _histPE[SiPM]->GetXaxis()->SetTitle("PEs");
      _histPE[SiPM]->SetLineColor(1);
    }

    _ntuple = new TNtuple("CRVNtuple","CRVNtuple","SiPM:photons:PEs:pulseHeight:pulseBeta:recoPEs:pulseTime:LEtime:chi2");
  }
}

WLSEventAction::~WLSEventAction()
{
  if(_mode==WLSSteppingAction::UseGeantOnly || _mode==WLSSteppingAction::UseGeantAndLookupTables)
  {
    for(int SiPM=0; SiPM<4; SiPM++)
    {
      delete _histP[0][SiPM];
      delete _histP[1][SiPM];
      delete _histT[0][SiPM];
      delete _histT[1][SiPM];
      delete _histPE[SiPM];
    }
  
    _ntuple->SaveAs("CRVNtuple.root");
    delete _ntuple;
  }

}

void WLSEventAction::BeginOfEventAction(const G4Event* evt)
{
  std::cout<<"Event # "<<evt->GetEventID()<<std::endl;

  WLSSteppingAction::Instance()->Reset();
}

void WLSEventAction::EndOfEventAction(const G4Event* evt)
{
  //create entry in lookup tables
  if(_mode==WLSSteppingAction::CreateLookupTables)
  {
    WLSDetectorConstruction *detector = WLSDetectorConstruction::Instance();

    mu2eCrv::LookupBin bin;

    bin.binNumber = _currentBin;  //gets set by WLSPrimaryGenerator

    bin.arrivalProbability=0;
    bin.timeDelays.clear();
    bin.fiberEmissions.clear();

    //prepare histograms
    int histTimeDifference[mu2eCrv::LookupBin::maxTimeDelays]={0}; //in ns
    int histEmissions[mu2eCrv::LookupBin::maxFiberEmissions]={0};
    int maxTimeDifference=0;
    int maxEmissions=0;
    int arrivingPhotons=0;

    //Lookup tables are created only for SiPM# 0 due to symmetry reasons
    const std::vector<WLSSteppingAction::PhotonInfo> &photonInfo = WLSSteppingAction::Instance()->GetPhotonInfo(0);
    for(size_t i=0; i<photonInfo.size(); i++)
    {
      //extract travel times
      int timeDifference=static_cast<int>(photonInfo[i]._arrivalTime+0.5); //rounded to full ns  
                                                                  //the WLS decay time has been set to 0 for mode=CreateLookupTables
                                                                  //so that the true travel time can be determined
                                                                  //(in WLSPrimaryGeneratorAction)
      if(timeDifference<0) timeDifference=0;
      if(timeDifference>=mu2eCrv::LookupBin::maxTimeDelays) continue;

      //extract number of fiber emissions
      int nEmissions=photonInfo[i]._fiberEmissions;
      if(nEmissions<0) nEmissions=0;
      if(nEmissions>=mu2eCrv::LookupBin::maxFiberEmissions) continue;

      //fill histogram bins
      histTimeDifference[timeDifference]++;   //fill this histogram bin
      histEmissions[nEmissions]++;

      if(timeDifference>maxTimeDifference) maxTimeDifference=timeDifference;
      if(nEmissions>maxEmissions) maxEmissions=nEmissions;

      arrivingPhotons++; //only count photons for which the arrival time and number of fiber emissions 
                         //can be stored in the histograms
    }
    std::cout<<"Arriving Photons: "<<arrivingPhotons<<std::endl;

    if(arrivingPhotons>0)
    {
      bin.arrivalProbability=static_cast<float>(arrivingPhotons)/static_cast<float>(_generatedPhotons);
                                                              //_generatedPhotons gets set by WLSPrimaryGeneratorAction
                                                              //after sending out all photons

      //store time time delay histogram in bin
      bin.timeDelays.reserve(maxTimeDifference+1);
      for(int i=0; i<=maxTimeDifference; i++)
      {
        float p=static_cast<float>(histTimeDifference[i])/static_cast<float>(arrivingPhotons);
        //multiplying the probability with the probability scale makes it possible to store it as an unsigned char
        unsigned char pChar = static_cast<unsigned char>(mu2eCrv::LookupBin::probabilityScale*p+0.5);
        bin.timeDelays.push_back(pChar);
      }

      //store fiber emission histogram in bin
      bin.fiberEmissions.reserve(maxEmissions+1);
      for(int i=0; i<=maxEmissions; i++)
      {
        float p=static_cast<float>(histEmissions[i])/static_cast<float>(arrivingPhotons);
        //multiplying the probability with the probability scale makes it possible to store it as an unsigned char
        unsigned char pChar = static_cast<unsigned char>(mu2eCrv::LookupBin::probabilityScale*p+0.5);
        bin.fiberEmissions.push_back(pChar);
      }
    }

    if(_verbose)
    {
      std::streamsize origPrecision = std::cout.precision();
      std::cout.precision(10);
      float p=bin.arrivalProbability;
      std::cout<<"Probability: "<<p<<std::endl;
      std::cout.precision(origPrecision);
      std::cout<<"Time Difference Probabilities: ";
      for(size_t i=0; i<bin.timeDelays.size(); i++) std::cout<<i<<"/"<<(int)bin.timeDelays[i]<<" ";
      std::cout<<std::endl;
      std::cout<<"Fiber Emissions Probabilities: ";
      for(size_t i=0; i<bin.fiberEmissions.size(); i++) std::cout<<i<<"/"<<(int)bin.fiberEmissions[i]<<" ";
      std::cout<<std::endl<<std::endl;
    }

    std::stringstream filename;
    filename<<"LookupTable_"<<_simType<<"_";
    filename.fill('0');
    filename.width(6);
    filename<<_minBin;
    if(evt->GetEventID()==0) std::remove(filename.str().c_str());  //remove existing file

//write some constants to the file before the first bin
    if(_simType==0 && _currentBin==0)
    {
      G4Material *scintillator = G4Material::GetMaterial("PolystyreneScint",true); //scintillator
      G4MaterialPropertiesTable *scintillatorPropertiesTable = scintillator->GetMaterialPropertiesTable();
      G4MaterialPropertyVector *rindexScintillator = scintillatorPropertiesTable->GetProperty("RINDEX");
  
      G4Material *fiber = G4Material::GetMaterial("PolystyreneFiber",true); //fiber
      G4MaterialPropertiesTable* fiberPropertiesTable = fiber->GetMaterialPropertiesTable();
      G4MaterialPropertyVector *rindexFiber = fiberPropertiesTable->GetProperty("RINDEX");

      mu2eCrv::LookupConstants LC;
      LC.version1           = 6;
      LC.version2           = 0;
      LC.reflector          = detector->GetReflectorOption();
      LC.halfThickness      = detector->GetScintillatorHalfThickness(),
      LC.halfWidth          = detector->GetScintillatorHalfWidth(), 
      LC.halfLength         = detector->GetScintillatorHalfLength(),
      LC.fiberSeparation    = detector->GetFiberSeparation(),
      LC.holeRadiusX        = detector->GetHoleRadiusX(),
      LC.holeRadiusY        = detector->GetHoleRadiusY(),
      LC.fiberRadius        = detector->GetClad2Radius();
      LC.scintillatorCornerRadius  = detector->GetScintillatorCornerRadius();
      LC.scintillatorBirksConstant = scintillator->GetIonisation()->GetBirksConstant();
      LC.WLSfiberDecayTime         = fiberPropertiesTable->GetConstProperty("WLSTIMECONSTANT");
      LC.Write(filename.str());

      mu2eCrv::LookupCerenkov LCerenkov;
      G4CerenkovNew *cerenkov = G4CerenkovNew::Instance();
      for(double beta=1.0; ; beta/=1.01)
      {
        double photonsScintillator=cerenkov->GetAverageNumberOfPhotons(CLHEP::eplus, beta, scintillator, rindexScintillator);
        double photonsFiber=cerenkov->GetAverageNumberOfPhotons(CLHEP::eplus, beta, fiber, rindexFiber);
        LCerenkov.photonsScintillator[beta]=photonsScintillator;
        LCerenkov.photonsFiber[beta]=photonsFiber;
        if(photonsScintillator==0 && photonsFiber==0) break;
      }
      LCerenkov.Write(filename.str());

      mu2eCrv::LookupBinDefinitions LBD;
      LBD.xBins     = WLSDetectorConstruction::Instance()->GetXBins();
      LBD.yBins     = WLSDetectorConstruction::Instance()->GetYBins();
      LBD.zBins     = WLSDetectorConstruction::Instance()->GetZBins();
      LBD.betaBins  = WLSDetectorConstruction::Instance()->GetBetaBins();
      LBD.thetaBins = WLSDetectorConstruction::Instance()->GetThetaBins();
      LBD.phiBins   = WLSDetectorConstruction::Instance()->GetPhiBins();
      LBD.rBins     = WLSDetectorConstruction::Instance()->GetRBins();
      LBD.Write(filename.str());
    }

//write the data to file
    bin.Write(filename.str());
  }

  //fill histograms if a simulation is run
  if(_mode==WLSSteppingAction::UseGeantOnly || _mode==WLSSteppingAction::UseGeantAndLookupTables)
  {
    for(int SiPM=0; SiPM<4; SiPM++)
    {
      _histP[0][SiPM]->Fill(WLSSteppingAction::Instance()->GetPhotonInfo(SiPM).size());
      _histP[1][SiPM]->Fill(WLSSteppingAction::Instance()->GetArrivalTimesFromLookupTables(SiPM).size());
      const std::vector<WLSSteppingAction::PhotonInfo> &photonInfo = WLSSteppingAction::Instance()->GetPhotonInfo(SiPM);
      const std::vector<double> &arrivalTimesFromLookupTables = WLSSteppingAction::Instance()->GetArrivalTimesFromLookupTables(SiPM);
      for(size_t i=0; i<photonInfo.size(); i++) _histT[0][SiPM]->Fill(photonInfo[i]._arrivalTime);
      for(size_t i=0; i<arrivalTimesFromLookupTables.size(); i++) _histT[1][SiPM]->Fill(arrivalTimesFromLookupTables[i]);
    }

    Draw(evt);

    std::cout<<"Photons: ";
    for(int SiPM=0; SiPM<4; SiPM++) std::cout<<_histP[0][SiPM]->GetMean()<<"/"<<_histP[1][SiPM]->GetMean()<<"  ";
    std::cout<<std::endl;
    std::cout<<"PEs: ";
    for(int SiPM=0; SiPM<4; SiPM++) std::cout<<_histPE[SiPM]->GetMean()<<"       ";
    std::cout<<std::endl;
    std::cout<<"Times: ";
    for(int SiPM=0; SiPM<4; SiPM++) std::cout<<_histT[0][SiPM]->GetMean()<<"/"<<_histT[1][SiPM]->GetMean()<<"  ";
    std::cout<<std::endl;
  }

  WLSStackingAction::Instance()->PrintStatus();
  WLSSteppingAction::Instance()->PrintFiberStats();
}

void WLSEventAction::Draw(const G4Event* evt) 
{
  double maxTime=200.0;

  mu2eCrv::MakeCrvSiPMCharges::ProbabilitiesStruct probabilities;
  probabilities._avalancheProbParam1 = 0.607;
  probabilities._avalancheProbParam2 = 2.7;
  probabilities._trapType0Prob = 0.0;
  probabilities._trapType1Prob = 0.0;
  probabilities._trapType0Lifetime = 5;
  probabilities._trapType1Lifetime = 50;
  probabilities._thermalRate = 3.0e-4;   
  probabilities._crossTalkProb = 0.05;

  int nPixelsX=40;
  int nPixelsY=40;
  double overvoltage=3.0;  //at 2017 test beam
  double timeConstant=13.3;
  double capacitance=8.84e-14;
  std::vector<std::pair<int,int> > inactivePixels = { {18,18}, {18,19}, {18,20}, {18,21},
                                                      {19,18}, {19,19}, {19,20}, {19,21},
                                                      {20,18}, {20,19}, {20,20}, {20,21},
                                                      {21,18}, {21,19}, {21,20}, {21,21} };

  static CLHEP::HepJamesRandom engine(1);
  static CLHEP::RandFlat randFlat(engine);
  static CLHEP::RandGaussQ randGaussQ(engine);
  static CLHEP::RandPoissonQ randPoissonQ(engine);
  mu2eCrv::MakeCrvSiPMCharges sim(randFlat,randPoissonQ,_photonMapFilename.c_str());
  sim.SetSiPMConstants(nPixelsX, nPixelsY, overvoltage, timeConstant, capacitance, probabilities, inactivePixels);

  mu2eCrv::MakeCrvWaveforms makeCrvWaveform;
  double singlePEWaveformPrecision=0.5;
  double singlePEWaveformStretchFactor=1.047;
  double singlePEWaveformMaxTime=100.0;
  double singlePEReferenceCharge=2.652e-13;
  double digitizationInterval = 12.55; //ns
  double digitizationInterval2 = 1.0; //ns
  double noise = 4.0e-4;
  double pedestal = 100; //ADC
  double ADCconversionFactor = 2300; //ADC/V
  double calibrationFactor = 394.6; //ADC*ns/PE
  double calibrationFactorPulseHeight = 11.4; //ADC/PE
  makeCrvWaveform.LoadSinglePEWaveform(_singlePEWaveformFilename.c_str(),
                                       singlePEWaveformPrecision, singlePEWaveformStretchFactor,
                                       singlePEWaveformMaxTime, singlePEReferenceCharge);

  mu2eCrv::MakeCrvDigis makeCrvDigis;

  boost::shared_ptr<mu2eCrv::MakeCrvRecoPulses> makeRecoPulses[4];

  double startTimeGlobal=G4UniformRand()*digitizationInterval;  //cannot be negative
  std::vector<std::pair<double, double> > siPMtimesAndCharges[4];
  std::vector<double> siPMchargesInPEs[4];
  std::vector<double> waveform[4], waveform2[4];
  std::vector<int16_t> ADCs[4], ADCs2[4];
  unsigned int TDC[4], TDC2[4];
  for(int SiPM=0; SiPM<4; SiPM++)
  {
    std::vector<std::pair<double, size_t> > photonTimes;
    if(_mode==WLSSteppingAction::UseGeantAndLookupTables)
    {
      const std::vector<double> &photonTimesTmp = WLSSteppingAction::Instance()->GetArrivalTimesFromLookupTables(SiPM);
      for(size_t i=0; i<photonTimesTmp.size(); i++) photonTimes.emplace_back(std::pair<double,size_t>(photonTimesTmp[i],0));
    }
    else
    {
      const std::vector<WLSSteppingAction::PhotonInfo> &photonInfo = WLSSteppingAction::Instance()->GetPhotonInfo(SiPM);
      for(size_t i=0; i<photonInfo.size(); i++) photonTimes.emplace_back(std::pair<double,size_t>(photonInfo[i]._arrivalTime,0));
    }
    int photons = photonTimes.size();

    std::vector<mu2eCrv::SiPMresponse> SiPMresponseVector;
    sim.Simulate(photonTimes, SiPMresponseVector, 0.0, maxTime);
    double PEs=0;
    double firstTime=NAN;
    for(size_t i=0; i<SiPMresponseVector.size(); i++)
    {
      siPMtimesAndCharges[SiPM].emplace_back(SiPMresponseVector[i]._time, SiPMresponseVector[i]._charge);
      siPMchargesInPEs[SiPM].push_back(SiPMresponseVector[i]._chargeInPEs);
      PEs+=SiPMresponseVector[i]._chargeInPEs;
      if(isnan(firstTime) || firstTime>SiPMresponseVector[i]._time) firstTime=SiPMresponseVector[i]._time;
    }
    _histPE[SiPM]->Fill(PEs);

    double startTime=startTimeGlobal;
    if(firstTime<startTime) startTime=firstTime*G4UniformRand();  //earliest charge cannot be before start time

    makeCrvWaveform.MakeWaveform(siPMtimesAndCharges[SiPM], waveform[SiPM], startTime, digitizationInterval);
    makeCrvWaveform.MakeWaveform(siPMtimesAndCharges[SiPM], waveform2[SiPM], startTime, digitizationInterval2);

    makeCrvWaveform.AddElectronicNoise(waveform[SiPM], noise, randGaussQ);
    makeCrvWaveform.AddElectronicNoise(waveform2[SiPM], noise, randGaussQ);

    makeCrvDigis.SetWaveform(waveform[SiPM], ADCconversionFactor, pedestal, startTime, digitizationInterval);
    ADCs[SiPM] = makeCrvDigis.GetADCs();
    TDC[SiPM] = makeCrvDigis.GetTDC();
    makeCrvDigis.SetWaveform(waveform2[SiPM], ADCconversionFactor, pedestal, startTime, digitizationInterval2);
    ADCs2[SiPM] = makeCrvDigis.GetADCs();
//    TDC2[SiPM] = makeCrvDigis.GetTDC();
    TDC2[SiPM] = lrint((TDC[SiPM]*digitizationInterval)/digitizationInterval2);

    float minADCdifference=5;   //in Offline: 40
    float defaultBeta=19.0;
    float minBeta=5.0;
    float maxBeta=40.0;
    float maxTimeDifference=15.0;
    float minPulseHeightRatio=0.7;
    float maxPulseHeightRatio=1.5;
    float LEtimeFactor=0.985;
    float pulseThreshold=0.5;
    float pulseAreaThreshold=5;
    float doublePulseSeparation=0.25;
    makeRecoPulses[SiPM]=boost::shared_ptr<mu2eCrv::MakeCrvRecoPulses>(new mu2eCrv::MakeCrvRecoPulses(minADCdifference, defaultBeta, minBeta, maxBeta,
                                                                                                      maxTimeDifference, minPulseHeightRatio, 
                                                                                                      maxPulseHeightRatio, LEtimeFactor,
                                                                                                      pulseThreshold, pulseAreaThreshold, doublePulseSeparation));
    std::vector<unsigned int> ADCsTmp(ADCs[SiPM].begin(),ADCs[SiPM].end());  //FIXME
    makeRecoPulses[SiPM]->SetWaveform(ADCsTmp, TDC[SiPM], digitizationInterval, pedestal, calibrationFactor, calibrationFactorPulseHeight);
    if(makeRecoPulses[SiPM]->GetPEs().size()>0) 
    {
      double pulseHeight=0;
      double recoPEs= 0;
      double pulseBeta=0;
      double pulseTime=0;
      double LEtime=0;
      double pulseChi2=0;
      for(size_t j=0; j<makeRecoPulses[SiPM]->GetPEs().size(); j++)
      {
         double recoPEsTmp = makeRecoPulses[SiPM]->GetPEs().at(j);
         if(recoPEsTmp>recoPEs)
         {
           recoPEs=recoPEsTmp;
           pulseHeight = makeRecoPulses[SiPM]->GetPulseHeights().at(j);
           pulseBeta = makeRecoPulses[SiPM]->GetPulseBetas().at(j);
           pulseTime = makeRecoPulses[SiPM]->GetPulseTimes().at(j);
           LEtime = makeRecoPulses[SiPM]->GetLEtimes().at(j);
           pulseChi2 = makeRecoPulses[SiPM]->GetPulseFitChi2s().at(j);
         }
      }
      _ntuple->Fill(SiPM,photons,PEs,pulseHeight,pulseBeta,recoPEs,pulseTime,LEtime,pulseChi2);
      _PEs[SiPM].push_back(PEs);
      _recoPEs[SiPM].push_back(recoPEs);
      _pulseTimes[SiPM].push_back(pulseTime);
      _LETimes[SiPM].push_back(LEtime);
      _pulseBetas[SiPM].push_back(pulseBeta);
      double avgPEs=0;
      double avgrecoPEs=0;
      double avgPulseTime=0;
      double avgLETime=0;
      double avgPulseBeta=0;
      for(size_t j=0; j<_PEs[SiPM].size(); j++) {avgPEs+=_PEs[SiPM][j];}
      for(size_t j=0; j<_recoPEs[SiPM].size(); j++) {avgrecoPEs+=_recoPEs[SiPM][j];}
      for(size_t j=0; j<_pulseTimes[SiPM].size(); j++) {avgPulseTime+=_pulseTimes[SiPM][j];}
      for(size_t j=0; j<_LETimes[SiPM].size(); j++) {avgLETime+=_LETimes[SiPM][j];}
      for(size_t j=0; j<_pulseBetas[SiPM].size(); j++) {avgPulseBeta+=_pulseBetas[SiPM][j];}
      avgPEs/=_PEs[SiPM].size();
      avgrecoPEs/=_recoPEs[SiPM].size();
      avgPulseTime/=_pulseTimes[SiPM].size();
      avgLETime/=_LETimes[SiPM].size();
      avgPulseBeta/=_pulseBetas[SiPM].size();
      std::cout<<"SiPM: "<<SiPM<<" PEs: "<<PEs<<"  avg: "<<avgPEs<<"     recoPEs: "<<recoPEs<<"("<<recoPEs/PEs<<") avg: "<<avgrecoPEs<<"("<<avgrecoPEs/avgPEs<<")   ";
      std::cout<<"avgtime:"<<avgPulseTime<<"/"<<avgLETime<<"       width:"<<pulseBeta<<"  avg:"<<avgPulseBeta<<"   height:"<<pulseHeight<<"  ";
      std::cout<<"nPulses:"<<makeRecoPulses[SiPM]->GetPEs().size()<<std::endl;
    }
  }

//Plotting things
  std::ostringstream s1;
  s1<<"waveform_"<<evt->GetEventID();

  gStyle->SetOptStat(0);
  TCanvas c(s1.str().c_str(),s1.str().c_str(),1000,1000);
  c.Divide(2,2);
  TGraph *graph[4]={NULL};
  TGraph *graph2[4]={NULL};
  TH1D *hist[4], *histSiPMResponse[4];

  if(evt->GetEventID()<20)
  {
  std::vector<TGraph*> graphVector;
  std::vector<TMarker*> markerVector;
  std::vector<TGaxis*> axisVector;

  for(int SiPM=0; SiPM<4; SiPM++)
  {
    c.cd(SiPM+1);

//Photon Arrival Times
    std::ostringstream s2, s3;
    s2<<"Photons_"<<evt->GetEventID()<<"__"<<SiPM;
    s3<<"Fiber: "<<SiPM/2<<",  Side: "<<SiPM%2;
    hist[SiPM]=new TH1D(s2.str().c_str(),s3.str().c_str(),100,0,maxTime);
    if(_mode==WLSSteppingAction::UseGeantAndLookupTables)
    {
      const std::vector<double> &photonTimesTmp = WLSSteppingAction::Instance()->GetArrivalTimesFromLookupTables(SiPM);
      for(size_t i=0; i<photonTimesTmp.size(); i++) hist[SiPM]->Fill(photonTimesTmp[i]);
    }
    else
    {
      const std::vector<WLSSteppingAction::PhotonInfo> &photonInfo = WLSSteppingAction::Instance()->GetPhotonInfo(SiPM);
      for(size_t i=0; i<photonInfo.size(); i++) hist[SiPM]->Fill(photonInfo[i]._arrivalTime);
    }

    hist[SiPM]->SetLineColor(kBlue);
    hist[SiPM]->GetXaxis()->SetTitle("t [ns]");
    hist[SiPM]->GetXaxis()->SetTitleOffset(0.8);
    hist[SiPM]->GetXaxis()->SetTitleSize(0.05);
    hist[SiPM]->GetYaxis()->SetTitle("Photons");
    hist[SiPM]->GetYaxis()->SetTitleOffset(0.8);
    hist[SiPM]->GetYaxis()->SetTitleSize(0.05);
    hist[SiPM]->GetYaxis()->SetAxisColor(kBlue);
    hist[SiPM]->GetYaxis()->SetTitleColor(kBlue);
    hist[SiPM]->GetYaxis()->SetLabelColor(kBlue);
    hist[SiPM]->Draw();

//SiPM response
    double scaleSiPMResponse = 1.0;
    double totalPEs=0;
    histSiPMResponse[SiPM]=new TH1D((s2.str()+"SiPMResponse").c_str(),"",100,0,maxTime);
    for(unsigned int j=0; j<siPMtimesAndCharges[SiPM].size(); j++)
    {
      histSiPMResponse[SiPM]->Fill(siPMtimesAndCharges[SiPM][j].first, siPMchargesInPEs[SiPM][j]*scaleSiPMResponse);
      totalPEs+=siPMchargesInPEs[SiPM][j];
    }
    histSiPMResponse[SiPM]->SetLineColor(kOrange-6);
    histSiPMResponse[SiPM]->Draw("histsame");

//waveforms with 1 ns bin width
    unsigned int n2 = ADCs2[SiPM].size();
    if(n2==0) continue;
    double *t2 = new double[n2];
    double *v2 = new double[n2];
    double histMax = hist[SiPM]->GetMaximum();
    double waveformMax = *std::max_element(ADCs2[SiPM].begin(),ADCs2[SiPM].end());
    double scale = histMax/waveformMax;
    for(unsigned int j=0; j<n2; j++)
    {
      t2[j]=(TDC2[SiPM]+j)*digitizationInterval2;
      v2[j]=ADCs2[SiPM][j];
      v2[j]*=scale;
    }
    graph2[SiPM]=new TGraph();
    graph2[SiPM]->SetTitle("");
    graph2[SiPM]->SetLineWidth(1);
    graph2[SiPM]->SetLineColor(kRed);
    graph2[SiPM]->DrawGraph(n2,t2,v2,"same");

    delete[] t2;
    delete[] v2;

//waveforms with 12.55 ns bin width
    unsigned int n = ADCs[SiPM].size();
    if(n==0) continue;
    double *t = new double[n];
    double *v = new double[n];
    for(unsigned int j=0; j<n; j++)
    {
      t[j]=(TDC[SiPM]+j)*digitizationInterval;
      v[j]=ADCs[SiPM][j];
      v[j]*=scale;
    }
    graph[SiPM]=new TGraph(n,t,v);
    graph[SiPM]->SetTitle("");
    graph[SiPM]->SetMarkerStyle(20);
    graph[SiPM]->SetMarkerSize(1.5);
    graph[SiPM]->SetMarkerColor(kRed);
    graph[SiPM]->Draw("sameP");

    delete[] t;
    delete[] v;

//fit
    unsigned int nPulse = makeRecoPulses[SiPM]->GetPEs().size();
    for(unsigned int pulse=0; pulse<nPulse; pulse++)
    {
      if(isnan(makeRecoPulses[SiPM]->GetPulseBetas().at(pulse))) continue;
      double tF1=makeRecoPulses[SiPM]->GetPulseStarts().at(pulse);
      double tF2=makeRecoPulses[SiPM]->GetPulseEnds().at(pulse);
      int nF=(tF2-tF1)/1.0 + 1;
      double *tF = new double[nF];
      double *vF = new double[nF];
      for(int iF=0; iF<nF; iF++)
      {
        double p0 = makeRecoPulses[SiPM]->GetPulseHeights().at(pulse)*TMath::E();
        double p1 = makeRecoPulses[SiPM]->GetPulseTimes().at(pulse);
        double p2 = makeRecoPulses[SiPM]->GetPulseBetas().at(pulse);
        tF[iF] = tF1 + iF*1.0;
        vF[iF] = p0*TMath::Exp(-(tF[iF]-p1)/p2-TMath::Exp(-(tF[iF]-p1)/p2));
        vF[iF]+=pedestal;
        vF[iF]*=scale;
        if(isnan(vF[iF])) nF=0;
      }
      if(nF>0)
      {
        TGraph *graphF=new TGraph();
        graphVector.push_back(graphF);
        graphF->SetTitle("");
        graphF->SetLineWidth(2);
        graphF->SetLineColor(kGreen);
        graphF->DrawGraph(nF,tF,vF,"same");
      }

      delete[] tF;
      delete[] vF;
    }

    TGaxis *axis = new TGaxis(maxTime*0.9,0,maxTime*0.9,histMax,0,histMax/scale,10,"+L");
    axisVector.push_back(axis);
    axis->SetTitle("ADC");
    axis->SetTitleOffset(-0.5);
    axis->SetTitleSize(0.05);
    axis->SetTitleColor(kRed);
    axis->SetLineColor(kRed);
    axis->SetLabelColor(kRed);
    axis->Draw("same");

    TGaxis *axisSiPMResponse = new TGaxis(maxTime,0,maxTime,histMax,0,histMax/scaleSiPMResponse,10,"+L");
    axisVector.push_back(axisSiPMResponse);
    axisSiPMResponse->SetTitle("SiPM charges [PE]");
    axisSiPMResponse->SetTitleOffset(0.85);
    axisSiPMResponse->SetTitleSize(0.05);
    axisSiPMResponse->SetTitleColor(kOrange-6);
    axisSiPMResponse->SetLineColor(kOrange-6);
    axisSiPMResponse->SetLabelColor(kOrange-6);
    axisSiPMResponse->Draw("same");
  }

  c.SaveAs((s1.str()+".C").c_str());

  for(int SiPM=0; SiPM<4; SiPM++)
  {
    delete hist[SiPM];
    delete histSiPMResponse[SiPM];
    if(graph[SiPM]) delete graph[SiPM];
    if(graph2[SiPM]) delete graph2[SiPM];
  }
  for(size_t i=0; i<graphVector.size(); i++) delete graphVector[i];
  for(size_t i=0; i<markerVector.size(); i++) delete markerVector[i];
  for(size_t i=0; i<axisVector.size(); i++) delete axisVector[i];
  }

  gStyle->SetOptStat(1111);
  TCanvas c1("Photons","Photons",1000,1000);
  c1.Divide(2,2);
  for(int SiPM=0; SiPM<4; SiPM++)
  {
    c1.cd(SiPM+1);
    gPad->SetLogy();
    _histP[0][SiPM]->Draw();
    gPad->Update();
    TPaveStats *stats0 = (TPaveStats*)_histP[0][SiPM]->FindObject("stats");
    stats0->SetTextColor(1);
    stats0->SetLineColor(1);
    double X1 = stats0->GetX1NDC();
    double Y1 = stats0->GetY1NDC();
    double X2 = stats0->GetX2NDC();
    double Y2 = stats0->GetY2NDC();
    _histP[1][SiPM]->Draw();
    gPad->Update();
    TPaveStats *stats1 = (TPaveStats*)_histP[1][SiPM]->FindObject("stats");
    stats1->SetTextColor(2);
    stats1->SetLineColor(2);
    stats1->SetX1NDC(X1);
    stats1->SetY1NDC(Y1-(Y2-Y1));
    stats1->SetX2NDC(X2);
    stats1->SetY2NDC(Y1);
    _histP[0][SiPM]->Draw();
    _histP[1][SiPM]->Draw("same");
  }      
  c1.SaveAs("Photons.C");

  TCanvas c2("ArrivalTimes","ArrivalTimes",1000,1000);
  c2.Divide(2,2);
  for(int SiPM=0; SiPM<4; SiPM++)
  {
    c2.cd(SiPM+1);
    gPad->SetLogy();
    _histT[0][SiPM]->Draw();
    gPad->Update();
    TPaveStats *stats0 = (TPaveStats*)_histT[0][SiPM]->FindObject("stats");
    stats0->SetTextColor(1);
    stats0->SetLineColor(1);
    double X1 = stats0->GetX1NDC();
    double Y1 = stats0->GetY1NDC();
    double X2 = stats0->GetX2NDC();
    double Y2 = stats0->GetY2NDC();
    _histT[1][SiPM]->Draw();
    gPad->Update();
    TPaveStats *stats1 = (TPaveStats*)_histT[1][SiPM]->FindObject("stats");
    stats1->SetTextColor(2);
    stats1->SetLineColor(2);
    stats1->SetX1NDC(X1);
    stats1->SetY1NDC(Y1-(Y2-Y1));
    stats1->SetX2NDC(X2);
    stats1->SetY2NDC(Y1);
    _histT[0][SiPM]->Draw();
    _histT[1][SiPM]->Draw("same");
  }      
  c2.SaveAs("ArrivalTimes.C");

  TCanvas c3("PEs","PEs",1000,1000);
  c3.Divide(2,2);
  for(int SiPM=0; SiPM<4; SiPM++)
  {
    c3.cd(SiPM+1);
    gPad->SetLogy();
    _histPE[SiPM]->Draw();
    _histPE[SiPM]->SaveAs(Form("PEs%i.root",SiPM));
  }      
  c3.SaveAs("PEs.C");
}

