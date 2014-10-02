#include <opencv2/core/core.hpp>
#include <iostream>
#include <iomanip>
#include <stdio.h>
#include <ctime>
#include <fstream>



//Mine
#include "convertAhandaPovRayToStandard.h"
#include "CostVolume/utils/reproject.hpp"
#include "CostVolume/utils/reprojectCloud.hpp"
#include "CostVolume/Cost.h"
#include "CostVolume/CostVolume.hpp"
#include "Optimizer/Optimizer.hpp"
#include "DepthmapDenoiseWeightedHuber/DepthmapDenoiseWeightedHuber.hpp"
// #include "OpenDTAM.hpp"
#include "graphics.hpp"
#include "set_affinity.h"
#include "Track/Track.hpp"

#include "utils/utils.hpp"


//debug
#include "tictoc.h"





const static bool valgrind=0;

//A test program to make the mapper run
using namespace cv;
using namespace cv::gpu;
using namespace std;

int App_main( int argc, char** argv );

void myExit(){
    ImplThread::stopAllThreads();
}
int main( int argc, char** argv ){

    initGui();

    int ret=App_main(argc, argv);
    myExit();
    return ret;
}


int App_main( int argc, char** argv )
{
    srand(time(NULL));
    rand();
    rand();
    cv::theRNG().state = rand();
    int numImg=500;

#if !defined WIN32 && !defined _WIN32 && !defined WINCE && defined __linux__ && !defined ANDROID
    pthread_setname_np(pthread_self(),"App_main");
#endif

    char filename[500];
    Mat image, cameraMatrix, R, T;
    vector<Mat> images,Rs,Ts,Rs0,Ts0;
    Mat ret;//a place to return downloaded images to
    
    ofstream file("outscale.csv");
    double reconstructionScale=5/5.;
    int inc=1;
    for(int i=0;i>0||inc>0;i+=inc){
        Mat tmp;
        sprintf(filename,"../../Trajectory_30_seconds/scene_%03d.png",i);
        convertAhandaPovRayToStandard("../../Trajectory_30_seconds",
                                      i,
                                      cameraMatrix,
                                      R,
                                      T);
        Mat image;
        cout<<"Opening: "<< filename << endl;
        if(inc>0){
        imread(filename, -1).convertTo(image,CV_32FC3,1.0/65535.0);
        resize(image,image,Size(),reconstructionScale,reconstructionScale);
        
        images.push_back(image.clone());
        }
        else
        {
            images.push_back(images[i]);
        }
        Rs.push_back(R.clone());
        Ts.push_back(T.clone());
//         Rs.push_back(Mat());
//         Ts.push_back(Mat());
        Rs0.push_back(R.clone());
        Ts0.push_back(T.clone());
        if(i==numImg-1)
            inc=-1;
    }
    numImg=numImg*2-2;
//     {//random first image
//         cout<<LieSub(RTToLie(Rs0[0],Ts0[0]),RTToLie(Rs0[1],Ts0[1]))<<endl;
//         Ts[0]=Ts0[1].clone();
//         Ts[1]=Ts0[1].clone();
//         randu(Ts[1] ,Scalar(-1),Scalar(1));
//         Ts[1]=Ts0[0]+Ts[1];
//         cout<<Ts[1]-Ts0[0]<<endl;
//         Rs[0]=Rs0[0].clone();
//         Rs[1]=Rs0[0].clone();
//     }
    CudaMem cret(images[0].rows,images[0].cols,CV_32FC1);
    ret=cret.createMatHeader();
    //Setup camera matrix
    double sx=reconstructionScale;
    double sy=reconstructionScale;
    cameraMatrix+=(Mat)(Mat_<double>(3,3) <<    0.0,0.0,0.5,
                                                0.0,0.0,0.5,
                                                0.0,0.0,0.0);
    cameraMatrix=cameraMatrix.mul((Mat)(Mat_<double>(3,3) <<    sx,0.0,sx,
                                                                0.0,sy ,sy,
                                                                0.0,0.0,1.0));
    cameraMatrix-=(Mat)(Mat_<double>(3,3) <<    0.0,0.0,0.5,
                                                0.0,0.0,0.5,
                                                0.0,0.0,0);
    int layers=256;
    int desiredImagesPerCV=500;
    int imagesPerCV=desiredImagesPerCV;
    int startAt=0;
//     {//offset init
//         Rs[startAt]=Rs[0].clone();
//         Rs[startAt+1]=Rs[1].clone();
//         Ts[startAt]=Ts[0].clone();
//         Ts[startAt+1]=Ts[1].clone();
//     }
    
    CostVolume cv(images[startAt],(FrameID)startAt,layers,0.015,0.0,Rs[startAt],Ts[startAt],cameraMatrix);
    
    //Old Way
    int imageNum=0;
    
    
    cv::gpu::Stream s;
    double totalscale=1.0;
    int tcount=0;
    int sincefail=0;
    for (int imageNum=startAt+1;imageNum<numImg;imageNum=(imageNum+1)%numImg){
        cout<<dec;
        T=Ts[imageNum].clone();
        R=Rs[imageNum].clone();
        image=images[imageNum];

        if(cv.count<imagesPerCV){
            cout<<"using: "<< imageNum<<endl;
            cv.updateCost(image, R, T);
            cudaDeviceSynchronize();
//             gpause();
//             for( int i=0;i<layers;i++){
//                 pfShow("layer",cv.downloadOldStyle(i), 0, cv::Vec2d(0, .5));
//                 usleep(1000000);
//             }
        }
        else{
            cudaDeviceSynchronize();
            //Attach optimizer
            Ptr<DepthmapDenoiseWeightedHuber> dp = createDepthmapDenoiseWeightedHuber(cv.baseImageGray,cv.cvStream);
            DepthmapDenoiseWeightedHuber& denoiser=*dp;
            Optimizer optimizer(cv);
            optimizer.initOptimization();
            GpuMat a(cv.loInd.size(),cv.loInd.type());
//             cv.loInd.copyTo(a,cv.cvStream);
            cv.cvStream.enqueueCopy(cv.loInd,a);
            GpuMat d;
            denoiser.cacheGValues();
            ret=image*0;
//             pfShow("A function", ret, 0, cv::Vec2d(0, layers));
//             pfShow("D function", ret, 0, cv::Vec2d(0, layers));
//             pfShow("A function loose", ret, 0, cv::Vec2d(0, layers));
//             pfShow("Predicted Image",ret,0,Vec2d(0,1));
//             pfShow("Actual Image",ret);
            
           
//                waitKey(0);
//                gpause();
            
            

            bool doneOptimizing; int Acount=0; int QDcount=0;
            do{
//                 cout<<"Theta: "<< optimizer.getTheta()<<endl;
//
//                 if(Acount==0)
//                     gpause();
//                a.download(ret);
//                pfShow("A function", ret, 0, cv::Vec2d(0, layers));
                
                

                for (int i = 0; i < 10; i++) {
                    d=denoiser(a,optimizer.epsilon,optimizer.getTheta());
                    QDcount++;
                    
//                    denoiser._qx.download(ret);
//                    pfShow("Q function:x direction", ret, 0, cv::Vec2d(-1, 1));
//                    denoiser._qy.download(ret);
//                    pfShow("Q function:y direction", ret, 0, cv::Vec2d(-1, 1));
//                    d.download(ret);
//                    pfShow("D function", ret, 0, cv::Vec2d(0, layers));
                }
                doneOptimizing=optimizer.optimizeA(d,a);
                Acount++;
//                 d.download(ret);
//                 pfShow("D function", ret, 0, cv::Vec2d(0, layers));
            }while(!doneOptimizing);
//             optimizer.lambda=.05;
//             optimizer.theta=10000;
//             optimizer.optimizeA(a,a);
            optimizer.cvStream.waitForCompletion();
             cv.loInd.download(ret);
            pfShow("loInd", ret, 0, cv::Vec2d(0, layers));
            a.download(ret);
            pfShow("A function loose", ret, 0, cv::Vec2d(0, layers));
            Mat diff=ret.clone();
            cv.loInd.download(ret);
            diff-=ret;
            pfShow("difference by reg", diff, 0, cv::Vec2d(-layers, layers));
//                gpause();
//             cout<<"A iterations: "<< Acount<< "  QD iterations: "<<QDcount<<endl;
//             pfShow("Depth Solution", optimizer.depthMap(), 0, cv::Vec2d(cv.far, cv.near));
//             imwrite("outz.png",ret);

            Track tracker(cv);
            Mat out=optimizer.depthMap();
            sprintf(filename,"/groundtruth/depth_%03d.png",cv.fid);
            Mat out16;
            out16=1/out;
            out16.convertTo(out16,CV_16UC3,10);
            cout<<"Mean:"<<mean(out16)[0]<<endl;
            imwrite(filename,out16);
//             if (tcount==3){
//                 out=cv.near-out;
//             }
            double m;
            minMaxLoc(out,NULL,&m);
            m=mean(out)[0];
            
                
            double sf=1;//(.25*cv.near/m);
            if(!(sf<100&&sf>.001)){
//                 file<<sf<<", fail!, "<<endl;
                cout<<"FAIL CV #: "<<tcount<<" sf: "<<sf<<endl;
                if(sf>100||sf<.001)
                    sf=1.0+.1-.2*(sf<1.0);
//                 gpause();
            }
            tracker.depth=out;
//             medianBlur(out,tracker.depth,3);
//             if(imageNum>180)
            imageNum=((imageNum-imagesPerCV)%numImg+numImg)%numImg;
//             else if (tcount>6)
//                  imageNum=((imageNum-imagesPerCV*2/3)%numImg+numImg)%numImg;
//                 if(imageNum<185)
//                 imageNum=180;
            
//             imageNum=30;
            assert(imageNum>=0);
//             if (imageNum>5)
//                 if(imagesPerCV==1)
            if (tcount>2)
                    imagesPerCV=desiredImagesPerCV;
//                 else
//                     imagesPerCV=1;
            sincefail++;
            
//             for(int i0=0;i0<=imagesPerCV;i0++){
//                 int i=(imageNum+i0)%numImg;
//                 tracker.addFrame(images[i]);
//                 if(!tracker.align()){
//                     imagesPerCV=max(i0-1,3);
// //                     if(i0==0&&sincefail>4){
// //                         cout<<"TRACKFAIL! RESTART RANDOM"<<endl;
// //                         sf=cv.near/.15;//failed so bad we need a new start
// // //                         randu(tracker.depth ,Scalar(0),Scalar(.15));
// //                         tracker.depth=.10;
// //                         tracker.pose=RTToLie(Rs[i-1],Ts[i-1]);
// //                         tracker.align();
// //                         sincefail=0;
// //                         Ts[i]=Ts[(i-1+numImg)%numImg].clone();
// //                         randu(Ts[i] ,Scalar(-1),Scalar(1));
// //                         Ts[i]=Ts[(i-1+numImg)%numImg]+Ts[i];
// //                         Rs[i]=Rs[(i-1+numImg)%numImg].clone();
// // //                         goto skip;
// //                     }
//                 }
//                
//                 LieToRT(tracker.pose,R,T);
//                 if(1||!i%10){
//                     Rs[i]=R.clone();
//                     Ts[i]=T.clone();
//                 }
//                 skip:
//                 Mat p,tp;
//                 p=tracker.pose;
//                 tp=RTToLie(Rs0[i],Ts0[i]);
// //                 {//debug
// //                     cout << "True Pose: "<< tp << endl;
// //                     cout << "True Delta: "<< LieSub(tp,tracker.basePose) << endl;
// //                     cout << "Recovered Pose: "<< p << endl;
// //                     cout << "Recovered Delta: "<< LieSub(p,tracker.basePose) << endl;
// //                     cout << "Pose Error: "<< p-tp << endl;
// //                 }
//                 cout<<i<<endl;
// //                 Mat tran1=Mat::eye(4,4,CV_64FC1);
// //                 ((Mat)(Mat_<double>(4,1) <<    0,0,-1.0/m,1)).copyTo(tran1.col(3));
// //                 Mat rotor=make4x4(rodrigues((Mat)(Mat_<double>(3,1) << 0,-45,0)*3.1415/180.0));
// //                 Mat tran2=Mat::eye(4,4,CV_64FC1);
// //                 ((Mat)(Mat_<double>(4,1) <<    0,0,3/m,1)).copyTo(tran2.col(3));
// //                 Mat view=tran2*rotor*tran1;
//                 Mat basePose=make4x4(RTToP(Rs[cv.fid],Ts[cv.fid]));
//                 Mat foundPose=make4x4(RTToP(Rs[i],Ts[i]));
// // //                 cout<<"view:\n"<< fixed << setprecision(3)<< view<<endl;
//                 reprojectCloud(images[i],images[cv.fid],tracker.depth,basePose,foundPose,cameraMatrix);
//             }
            
//             if (tcount>6&&imagesPerCV>20)
//             {
//                 int jump=imagesPerCV*2/3;
//                 imageNum=(imageNum+jump)%numImg;
//                 imagesPerCV-=jump;
//                 assert(imagesPerCV>0);
//             }
            
            for (int a=0;a<360;a+=1)
            {
            Mat tran1=Mat::eye(4,4,CV_64FC1);
            ((Mat)(Mat_<double>(4,1) <<    0,-100,-400,1)).copyTo(tran1.col(3));
            Mat rotor=make4x4(rodrigues((Mat)(Mat_<double>(3,1) << 0,a,0)*3.1415926535/180.0)*
                rodrigues((Mat)(Mat_<double>(3,1) << 90,0,0)*3.1415926535/180.0)
            );
            Mat tran2=Mat::eye(4,4,CV_64FC1);
            ((Mat)(Mat_<double>(4,1) <<    0,0,3/m,1)).copyTo(tran2.col(3));
            Mat view=tran2*rotor*tran1;
            Mat basePose=make4x4(RTToP(Rs[cv.fid],Ts[cv.fid]));
            Mat foundPose=make4x4(RTToP(Rs[imageNum],Ts[imageNum]));
            reprojectCloud(images[imageNum],images[cv.fid],tracker.depth,basePose,view,cameraMatrix);
            }
            cv=CostVolume(images[imageNum],(FrameID)imageNum,layers,cv.near/sf,0.0,Rs0[imageNum],Ts0[imageNum],cameraMatrix);
            totalscale*=sf;
            file<<imageNum<<", "<<sf<<", "<<imagesPerCV<<endl;
//             file.sync_with_stdio();
            if(tcount==7){
                totalscale=1.0f;
            }
            tcount++;
            cout<<"CV #: "<<tcount<<" Total Scale: "<<totalscale<<endl;
            s=optimizer.cvStream;
//             for (int imageNum=0;imageNum<numImg;imageNum=imageNum+1){
//                 reprojectCloud(images[imageNum],images[0],optimizer.depthMap(),RTToP(Rs[0],Ts[0]),RTToP(Rs[imageNum],Ts[imageNum]),cameraMatrix);
//             }
            a.download(ret);
            
            
        }
        s.waitForCompletion();// so we don't lock the whole system up forever
    }
    exit:
    s.waitForCompletion();
    Stream::Null().waitForCompletion();
    return 0;
}


