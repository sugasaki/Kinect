﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;

using System.Threading;

using Microsoft.Kinect;
//using System.Threading;

namespace KinectExile
{
    /// <summary>
    /// MainWindow.xaml の相互作用ロジック
    /// </summary>
    public partial class MainWindow : Window
    {

        private KinectExile.Models.RingBuffer ringbuf;

        private const int BytesPerPixel = 4;
        private const int max_player = 4; //分身の数

        private const int fps = 30;
        //private const double interval = 0.5;
        private const double interval =2.5;

        private  double buff_sec;　//n秒分のバッファ領域を作成する

        private int colorPixelData_Length;

        #region field

        private KinectSensor kinectDevice;

        private short[] _depthPixelData;
        private byte[] _colorPixelData;


        private const int buffer_sec = 3;

        private int screenImageStride;


        private Rect image_size;

        #endregion



        #region プロパティ

        private WriteableBitmap _overray_bitmap;
        private WriteableBitmap _room_bitmap;
        

        private Int32Rect _screenImageRect;

        #endregion

        bool isContinue = true;


        public MainWindow()
        {
            InitializeComponent();
            buff_sec = Math.Ceiling(max_player * interval);　//n秒分のバッファ領域を作成する
        }


        private void Window_Loaded(object sender, RoutedEventArgs e)
        {
            string ret_message;
            init_kinect(out ret_message);

            ////レンダリング時にKinectデータを取得し描画
            //CompositionTarget.Rendering += compositionTarget_rendering;

        }

        private void Window_Closed(object sender, EventArgs e)
        {
            StopKinect();
        }

        /// <summary>
        /// kinect初期化
        /// </summary>
        private bool init_kinect(out string ret_message)
        {
            ret_message = string.Empty;
            try
            {
                // Kinectデバイスを取得する
                kinectDevice = KinectSensor.KinectSensors.FirstOrDefault(x => x.Status == KinectStatus.Connected);
                if (kinectDevice == null) return false;

                kinectDevice.DepthStream.Enable(DepthImageFormat.Resolution320x240Fps30);
                kinectDevice.ColorStream.Enable(ColorImageFormat.RgbResolution640x480Fps30);
                kinectDevice.SkeletonStream.Enable();


                //ビットマップデータ初期化
                var depthStream = kinectDevice.DepthStream;
                _depthPixelData = new short[kinectDevice.DepthStream.FramePixelDataLength];
                _colorPixelData = new byte[kinectDevice.ColorStream.FramePixelDataLength];


                //Bitmap初期化
                _overray_bitmap = new WriteableBitmap(depthStream.FrameWidth, depthStream.FrameHeight, 96, 96, PixelFormats.Bgra32, null);
                _room_bitmap = new WriteableBitmap(depthStream.FrameWidth, depthStream.FrameHeight, 96, 96, PixelFormats.Bgra32, null);
                _screenImageRect = new Int32Rect(0, 0, (int)Math.Ceiling(_overray_bitmap.Width), (int)Math.Ceiling(_overray_bitmap.Height));

                //リングバッファの初期化
                ringbuf = new Models.RingBuffer();
                bool ret = ringbuf.init_ringbuffer(buff_sec, kinectDevice.ColorStream.FramePixelDataLength, kinectDevice.DepthStream.FramePixelDataLength, out ret_message);
                if (ret == false)
                {
                    MessageBox.Show(ret_message);
                    return false;
                }

                //kinectDevice.AllFramesReady += kinect_AllFramesReady;
                kinectDevice.AllFramesReady += new EventHandler<AllFramesReadyEventArgs>( kinect_AllFramesReady );

                kinectDevice.Start();

                //画像サイズ
                //image_size.Width = kinectDevice.ColorStream.FrameWidth;
                //image_size.Height = kinectDevice.ColorStream.FrameHeight;




                // 音声入出力スレッド
                Thread thread = new Thread(new ThreadStart(() =>
                {
                    while (isContinue)
                    {
                        // 音声を入力し、スピーカーに出力する
                        RenderScreen2();
                    }
                }));

                // スレッドの動作を開始する
                thread.Start();


                return true;
            }
            catch (Exception ex)
            {
                ret_message = ex.Message;
                MessageBox.Show(ex.Message);
                return false;
            }
        }


        /// <summary>
        /// Kinectの動作を停止する
        /// </summary>
        /// <param name="kinect"></param>
        public void StopKinect()
        {
            if (kinectDevice != null)
            {
                if (kinectDevice.IsRunning)
                {
                    isContinue = false;
                    
                    // Kinectの停止と、ネイティブリソースを解放する
                    kinectDevice.Stop();
                    kinectDevice.Dispose();
                }
            }
        }



        /// <summary>
        /// Kinectデータを取得
        /// </summary>
        private void kinect_AllFramesReady(object sender, AllFramesReadyEventArgs e)
        {
            using (ColorImageFrame colorFrame = e.OpenColorImageFrame())
            {
                using (DepthImageFrame depthFrame = e.OpenDepthImageFrame())
                {
                    using (SkeletonFrame skeletonFrame = e.OpenSkeletonFrame())
                    {
                        SaveBuffer(colorFrame, depthFrame, skeletonFrame);
                    }
                }
            }
        }




        /// <summary>
        /// キネクトの画像をバッファへ保存する
        /// </summary>
        /// <param name="kinectDevice"></param>
        /// <param name="colorFrame"></param>
        /// <param name="depthFrame"></param>
        private void SaveBuffer(ColorImageFrame colorFrame, DepthImageFrame depthFrame, SkeletonFrame skeletonFrame)
        {
            if (kinectDevice == null || depthFrame == null || colorFrame == null || skeletonFrame == null) return;

            ColorImageStream colorStream = kinectDevice.ColorStream;
            DepthImageStream depthStream = kinectDevice.DepthStream;
            screenImageStride = kinectDevice.DepthStream.FrameWidth * colorFrame.BytesPerPixel;

            int colorStride = colorFrame.BytesPerPixel * colorFrame.Width; //4×画像幅

            int ImageIndex = 0;

            depthFrame.CopyPixelDataTo(_depthPixelData);
            colorFrame.CopyPixelDataTo(_colorPixelData);


            ColorImagePoint[] colorPoint = new ColorImagePoint[depthFrame.PixelDataLength];
            short[] depthPixel = new short[depthFrame.PixelDataLength];
            kinectDevice.MapDepthFrameToColorFrame(depthFrame.Format, depthPixel, colorFrame.Format, colorPoint);


            byte[] byteRoom = new byte[depthFrame.Height * screenImageStride + 5];
            byte[] bytePlayer = new byte[depthFrame.Height * screenImageStride + 5];


            double[] depth = new double[depthFrame.Height * screenImageStride+1];
            int[] playerIndexArray = new int[depthFrame.Height * screenImageStride+1];


            for (int depthY = 0; depthY < depthFrame.Height; depthY++)
            {
                for (int depthX = 0; depthX < depthFrame.Width; depthX++)
                {
                    ImageIndex += colorFrame.BytesPerPixel;
                    int depthPixelIndex = depthX + (depthY * depthFrame.Width);

                    int playerIndex = _depthPixelData[depthPixelIndex];// &DepthImageFrame.PlayerIndexBitmask; //人のID取得

                    int x = Math.Min(colorPoint[depthPixelIndex].X, colorStream.FrameWidth - 1);
                    int y = Math.Min(colorPoint[depthPixelIndex].Y, colorStream.FrameHeight - 1);

                    int colorPixelIndex = (x * colorFrame.BytesPerPixel) + (y * colorStride);

                    if (playerIndex != 0)
                    {
                        bytePlayer[ImageIndex] = _colorPixelData[colorPixelIndex];           //Blue    
                        bytePlayer[ImageIndex + 1] = _colorPixelData[colorPixelIndex + 1];   //Green
                        bytePlayer[ImageIndex + 2] = _colorPixelData[colorPixelIndex + 2];   //Red
                        bytePlayer[ImageIndex + 3] = 0xFF;                             //Alpha

                        //ピクセル深度を取得
                        depth[ImageIndex] = _depthPixelData[depthPixelIndex] >> DepthImageFrame.PlayerIndexBitmaskWidth;
                        playerIndexArray[ImageIndex] = playerIndex; 
                    }
                    else
                    {
                        byteRoom[ImageIndex] = _colorPixelData[colorPixelIndex];           //Blue    
                        byteRoom[ImageIndex + 1] = _colorPixelData[colorPixelIndex + 1];   //Green
                        byteRoom[ImageIndex + 2] = _colorPixelData[colorPixelIndex + 2];   //Red
                        byteRoom[ImageIndex + 3] = 0xFF;                             //Alpha
                    }

                }
            }

            ////byteからビットマップへ書出し
            //_room_bitmap.WritePixels(_screenImageRect, byteRoom, screenImageStride, 0);
            //room_image.Source = _room_bitmap;


            //人の情報をリングバッファへ保存
            ringbuf.save_framedata(bytePlayer);
            ringbuf.save_depthdata(_depthPixelData);
            ringbuf.save_depthLength(depth);
            ringbuf.save_playerIndexdata(playerIndexArray);

            colorPixelData_Length = _colorPixelData.Length;

            ringbuf.set_nextframe();

            //RenderScreen2();
        }



        /// <summary>
        /// キネクトの画像をビットマップデータに書き出す
        /// </summary>
        /// <param name="kinectDevice"></param>
        /// <param name="colorFrame"></param>
        /// <param name="depthFrame"></param>
        private void RenderScreen2()
        {
            if (screenImageStride == 0) return;
            byte[] bytePlayer = new byte[(int)image_size.Height * screenImageStride];

            int frame = (int) Math.Ceiling(fps * interval); //15frame=0.5sec

            byte r = 0, g = 0, b = 0;

            for (int index = 0; index < colorPixelData_Length; index += BytesPerPixel)
            {

                for (int i = 0; i < max_player; i++)
                {
                    double max_depth = double.MinValue;

                    int iframe = ringbuf.buffer_index - (max_player * frame);
                    
                    //int iframe = i == (max_player-1) ? ringbuf.buffer_index : ringbuf.buffer_index + (frame * i);
                    iframe += frame;//過去へ戻る
                    if (i == max_player - 1) iframe = ringbuf.buffer_index;

                    var PlayerIndex = ringbuf.get_PlayerIndexData(iframe);

                    if (PlayerIndex[index] > 0)
                    {
                        var depth = ringbuf.get_depth_length(iframe, index);
                        if (depth > max_depth)
                        {
                            r = ringbuf.get_rgb_frame(iframe, index);
                            g = ringbuf.get_rgb_frame(iframe, index + 1);
                            b = ringbuf.get_rgb_frame(iframe, index + 1);
                            max_depth = depth;
                        }
                    }
                }

                bytePlayer[index] = r;
                bytePlayer[index + 1] = g;
                bytePlayer[index + 2] = b;
            }

            //_overray_bitmap.WritePixels(_screenImageRect, bytePlayer, screenImageStride, 0);

            Dispatcher.Invoke( (Action)(() => { 
                //human_image1.Source = _overray_bitmap;
            }),null);


            //byteからビットマップへ書出し

        }



    }
}
