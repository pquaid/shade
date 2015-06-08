/*
       Shade.c

       This program reads an object specified in
       3-dimensional coordinates and displays it.
       To quit the program, just press a key or
       click the left mouse button.

       Usage: Shade InputFile


       Compile using DICE with the command line:

        dcc shade.c -oshade -lm

       if you don't have AmigaDOS 2.0 (or more
       specifically, mathieeesing#?.library), use:

        dcc shade.c -oshade -lm -ffp

       That'll use FFP floats rather than IEEE.

*/


#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "exec/types.h"
#include "exec/memory.h"
#include "intuition/intuition.h"
#include "graphics/gfxmacros.h"
#include <time.h>

/*  These constants define the size of the            */
/*  screen.  You can set MAXX to 320 or 640 and       */
/*  MAXY to 200 or 400.  All the other routines       */
/*  will adjust to these values.                      */

#define MAXX            640
#define MAXY            400
#define HALFX           (MAXX / 2)
#define HALFY           (MAXY / 2)
#define PI              3.14159
#define ASPECT          1.4

/*  A few error messages                              */

#define NO_MEMORY  "Could not allocate memory"
#define BAD_FILE   "Error reading object definition"
#define BAD_PARAM  "Parameter out of range"



/* 3D_Point : One point from the input file.          */

    typedef struct {
        float X,Y,Z;
    } Point_3D;



/* Display_Point: the display position of a point.    */
/* The Z coordinate is a fake - it's really just a    */
/* positive or negative integer to tell you whether   */
/* the point is in front of or behind you,            */
/* respectively.                                      */

    typedef struct {
        short X,Y,Z;
    } Display_Point;



/* Face: the index of the first and last vertices     */
/*       for this face.  The index refers to the      */
/*       Connections array, which contains all the    */
/*       actual vertex information.                   */

    typedef struct {
        short  start,end;
        float  distance;
    } Face;



/* The libraries we'll need.  With DICE we didn't     */
/* actually have to declare these - it would handle   */
/* everything automatically.                          */

    void  *IntuitionBase = NULL,
          *GfxBase = NULL;


/* Windows and screens.  I open two of each to        */
/* implement a poor man's double buffering.           */

    struct Window *window1 = 0,
                  *window2 = 0,
                  *frontwindow;

    struct Screen *screen1 = 0,
                  *screen2 = 0;

    struct RastPort *rp;



    struct NewScreen MyNewScreen =
                { 0,0,
                  MAXX,MAXY,4,
                  0,0,
                  ((MAXX > 320) ? HIRES : 0) |
                  ((MAXY > 200) ? LACE  : 0),
                  CUSTOMSCREEN,NULL,(UBYTE *) "",
                  NULL,NULL};

    struct NewWindow MyNewWindow =
                {0,0,
                 MAXX,MAXY,
                 0,0,
                 VANILLAKEY | MOUSEBUTTONS,
                 BACKDROP | BORDERLESS | ACTIVATE,
                 NULL,NULL,(UBYTE *) "",NULL,NULL,
                 MAXX,MAXY,MAXX,MAXY,
                 CUSTOMSCREEN};

/* These are the structures needed to use the         */
/* graphics.library Area routines.                    */

    short  AreaBuffer1[60],
           AreaBuffer2[60];
    struct TmpRas tmpras1, tmpras2;
    struct AreaInfo areainfo1, areainfo2;

    short  *RasterBuffer1 = NULL,
           *RasterBuffer2 = NULL;

/* The input file.                                    */

    FILE   *ObjectFile = 0;


/* The totals, which will also define the size of the */
/* buffers.  TotalPoints is the total number of world */
/* data points.  TotalFaces is the total number of    */
/* faces to draw.  ConnectLen is the total number of  */
/* connections the faces will define.  In other words,*/
/* it's somthing like TotalFaces * (points per face). */

    long  TotalPoints,
          TotalFaces,
          ConnectLen;


/* The arrays.  World_Data represents the actual data */
/* points.                                            */

    Point_3D    *World_Data = NULL;


/* Face_List is an array of polygon definitions,      */
/* defined as a range of vertices in the connection   */
/* array.                                             */

    Face        *Face_List = NULL;


/* This array holds the points that make up each face.*/
/* The Face_List tells where each face begins and     */
/* ends.                                              */

    short       *Connections = NULL;


/* Display represents the world data points           */
/* translated into their actual display positions.    */

    Display_Point *Display = NULL;


/* The variables that define what the display will    */
/* actually look like.  In this program they are just */
/* assigned reasonable values, but you can set them   */
/* to almost anything.                                */

    Point_3D    At,        /* Target point            */
                From,      /* Camera position         */
                Light,     /* Light position          */
                UP,        /* Vector pointing UP      */
                V[3];      /* Transformation matrix   */


/* Multipliers that actually do several things.  They */
/* scale the coordinates up to screen coordinates,    */
/* define the field of view, and correct for non-     */
/* square pixels.                                     */

    float       MultX,MultY;


/* These two values define the relative contributions */
/* each type of light.  They don't have to add to 1.0 */

    float       Ambient, Diffuse, Specular, Sharpness;


/* These are the area patterns used for dithering.    */

    short       EvenCheck[] = {0x5555,0xAAAA},
                OddCheck[]  = {0x8888,0x2222},
                SolidPattern = 0xFFFF;


/*  The following routines are general vector &       */
/*  manipulation routines, but just the ones used in  */
/*  this program.                                     */

/* Magnitude: calculate |v| (magnitude of vector v) */

float Magnitude(Point_3D *v)
{
    return (fsqrt(v->X*v->X + v->Y*v->Y + v->Z*v->Z));
}



/* Normalize: Make |v| = 1 (make its length = 1)    */

void Normalize(Point_3D *v)
{
    float mag;

    if ((mag = Magnitude(v)) == 0.0)
        return;

    v->X /= mag;
    v->Y /= mag;
    v->Z /= mag;
}

/*  Minus: Calculate r = v1 - v2                      */
/*         (make a vector pointing from v2 to v1)     */

void Minus(Point_3D v1,Point_3D v2, Point_3D *r)
{
    r->X = v1.X - v2.X;
    r->Y = v1.Y - v2.Y;
    r->Z = v1.Z - v2.Z;
}


/*  VectorMatrix: Calculate vm, where v is 1x3 and    */
/*                m is 3x3.  Result in r, which can   */
/*                be the same as v.                   */

void VectorMatrix(Point_3D v, Point_3D m[3], Point_3D *r)
{
    Point_3D    temp;

    temp.X = v.X*m[0].X + v.Y*m[1].X + v.Z*m[2].X;
    temp.Y = v.X*m[0].Y + v.Y*m[1].Y + v.Z*m[2].Y;
    temp.Z = v.X*m[0].Z + v.Y*m[1].Z + v.Z*m[2].Z;

    r->X = temp.X;
    r->Y = temp.Y;
    r->Z = temp.Z;
}


/*  CrossProduct: calculate r = v1 X v2.  r can be    */
/*                the same thing as v1 or v2.         */

void CrossProduct(Point_3D v1, Point_3D v2, Point_3D *r)
{
    Point_3D    temp;
    short       i;

    temp.X = v1.Y*v2.Z - v2.Y*v1.Z;
    temp.Y = v2.X*v1.Z - v1.X*v2.Z;
    temp.Z = v1.X*v2.Y - v2.X*v1.Y;

    r->X = temp.X;
    r->Y = temp.Y;
    r->Z = temp.Z;
}


/*  DotProduct: calculate the dot product of v1.v2    */

float DotProduct(Point_3D v1, Point_3D v2)
{
    return (v1.X*v2.X + v1.Y*v2.Y + v1.Z*v2.Z);
}


/* RotateX: rotate the given point theta radians      */
/*          about a line parallel to the x axis that  */
/*          goes through the point Center.            */

/* This routine first translates the point to Center, */
/* then rotates the point using the following matrix: */

/*         |   1   0    0    |                        */
/*         |   0  cost sint  |                        */
/*         |   0 -sint cost  |                        */

/* Then it translates back by -Center.                */


void RotateX(Point_3D Initial, Point_3D Center,
             float theta, Point_3D *Result)
{
    float   temp, sine, cosine;

    Minus(Initial,Center,&Result);

    sine = fsin(theta);
    cosine = fcos(theta);

    temp = Result->Y;

    Result->Y = temp * cosine + Result->Z * -sine;
    Result->Z = temp * sine + Result->Z * cosine;

    Result->X += Center.X;
    Result->Y += Center.Y;
    Result->Z += Center.Z;
}


/* RotateY: rotate the given point theta radians      */
/*          about a line parallel to the y axis that  */
/*          goes through the point Center.            */

/* This routine first translates the point to Center, */
/* then rotates the point using the following matrix: */

/*         |  cost  0  -sint |                        */
/*         |   0    1    0   |                        */
/*         |  sint  0   cost |                        */

/* Then it translates back by -Center.                */


void RotateY(Point_3D Initial, Point_3D Center,
             float theta, Point_3D *Result)
{
    float   temp, sine, cosine;

    Minus(Initial,Center,Result);

    sine = fsin(theta);
    cosine = fcos(theta);

    temp = Result->X;

    Result->X = temp * cosine + Result->Z * sine;
    Result->Z = temp * -sine + Result->Z * cosine;

    Result->X += Center.X;
    Result->Y += Center.Y;
    Result->Z += Center.Z;
}



/* RotateZ: rotate the given point theta radians      */
/*          about a line parallel to the z axis that  */
/*          goes through the point Center.            */

/* This routine first translates the point to Center, */
/* then rotates the point using the following matrix: */

/*         |  cost  sint  0  |                        */
/*         | -sint  cost  0  |                        */
/*         |   0     0    1  |                        */

/* Then it translates back by -Center.                */


void RotateZ(Point_3D Initial, Point_3D Center,
             float theta, Point_3D *Result)
{
    float   temp, sine, cosine;

    Minus(Initial,Center,Result);

    sine = fsin(theta);
    cosine = fcos(theta);

    temp = Result->X;

    Result->X = temp * cosine + Result->Y * -sine;
    Result->Y = temp * sine + Result->Y * cosine;

    Result->X += Center.X;
    Result->Y += Center.Y;
    Result->Z += Center.Z;
}




/* If for some reason I can't open a screen or        */
/* something else goes haywire, I call this           */
/* routine to notify the user and bug out cleanly     */

void Quit(char *msg)
{
    /* Close the input file */

    if (ObjectFile)
        fclose(ObjectFile);

    /* Free all the memory */

    if (RasterBuffer1)
        FreeRaster(RasterBuffer1,MAXX,MAXY);
    if (RasterBuffer2)
        FreeRaster(RasterBuffer2,MAXX,MAXY);
    if (Display)
        FreeMem(Display,TotalPoints*sizeof(Display_Point));
    if (World_Data)
        FreeMem(World_Data,TotalPoints*sizeof(Point_3D));
    if (Face_List)
        FreeMem(Face_List,TotalFaces*sizeof(Face));
    if (Connections)
        FreeMem(Connections,ConnectLen*sizeof(short));

    /* Close the windows and screens */

    if (window1)
        CloseWindow(window1);
    if (screen1)
        CloseScreen(screen1);

    if (window2)
        CloseWindow(window2);
    if (screen2)
        CloseScreen(screen2);

    /* Close the libraries */

    if (IntuitionBase)
        CloseLibrary(IntuitionBase);
    if (GfxBase)
        CloseLibrary(GfxBase);

    /* Tell the user and leave */

    puts(msg);
    exit(0);
}



/* GetMemory: allocate "amount" bytes of memory, and  */
/*            abort the program if it can't be had.   */

void *GetMemory(long amount)
{
    void  *temp;

    temp = (void *) AllocMem(amount,MEMF_PUBLIC);
    if (!temp)
        Quit(NO_MEMORY);
    return (temp);
}


/*  Read the object definition from the input file.   */
/*  The format of the definition file is as follows:  */

/*      Number of Points                              */
/*      Number of Faces                               */
/*      Number of connections                         */
/*         (i.e. Faces * points per face)             */

/*      3D points, expressed as X Y Z.                */
/*      Face definitions, expressed as a series of    */
/*         vertex numbers (based at 1), with a        */
/*         negative vertex specifying the last vertex */
/*         in the face.                               */

/*  This routine also allocates memory for the world  */
/*  coordinates, face list, and connection array.     */

/*  Since my version of DICE won't scanf floating     */
/*  point numbers yet, I had to read them in as longs */
/*  and convert them.                                 */

/*  Apparently there are some object formats used by  */
/*  Amiga graphics programs that you could use with   */
/*  this program - I'm not familiar with them, but    */
/*  you could probably make this program read and     */
/*  understand them.                                  */


void ReadObjectFile(char *fname)
{
    short  FaceNum,i;
    long   vertex;
    long   TempX,TempY,TempZ;

    ObjectFile = fopen(fname, "r");
    if (ObjectFile) {
        if (fscanf(ObjectFile, "%ld",&TotalPoints)==EOF)
            Quit(BAD_FILE);
        if (TotalPoints < 1)
            Quit(BAD_PARAM);

        World_Data = GetMemory(TotalPoints*sizeof(Point_3D));
        Display = GetMemory(TotalPoints*sizeof(Display_Point));

        if (fscanf(ObjectFile, "%ld",&TotalFaces)==EOF)
            Quit(BAD_FILE);
        if (TotalFaces < 1)
            Quit(BAD_PARAM);
        Face_List = GetMemory((TotalFaces+1)*sizeof(Face));

        if (fscanf(ObjectFile, "%ld",&ConnectLen)==EOF)
            Quit(BAD_FILE);
        if (ConnectLen < 1)
            Quit(BAD_PARAM);
        Connections = GetMemory(ConnectLen*sizeof(short));

        for (i=0; i<TotalPoints; i++) {
            if (fscanf(ObjectFile,"%ld %ld %ld",
                &TempX,&TempY,&TempZ)==EOF)
                Quit(BAD_FILE);
            World_Data[i].X = (float) (TempX / 10000.0);
            World_Data[i].Y = (float) (TempY / 10000.0);
            World_Data[i].Z = (float) (TempZ / 10000.0);
        }

        FaceNum = 0;
        Face_List[0].start = 0;

        for (i=0; i<ConnectLen; i++) {
            if (fscanf(ObjectFile, "%ld",&vertex)==EOF)
                Quit(BAD_FILE);
            if (vertex < 0) {
                Connections[i] = (-vertex) - 1;
                Face_List[FaceNum++].end = i;
                Face_List[FaceNum].start = i+1;
            } else
                Connections[i] = vertex - 1;
        }

        fclose(ObjectFile);
    } else
        Quit("Could not open input file");
}


/* Open a couple of screens and windows.  This        */
/* program uses two of each for double buffering.     */
/* Also, this routine jump-starts the double          */
/* buffering process.                                 */

void OpenDisplay()
{
    struct ViewPort  *vp;
    long  i;

    if (!(screen1 = (void *) OpenScreen(&MyNewScreen)))
        Quit("Could not open screen");
    ShowTitle(screen1, FALSE);

    if (!(screen2 = (void *) OpenScreen(&MyNewScreen)))
        Quit("Could not open screen");
    ShowTitle(screen2, FALSE);

    MyNewWindow.Screen = screen1;

    if (!(window1 = (struct Window *)
                            OpenWindow(&MyNewWindow)))
        Quit("Could not open the window");

    MyNewWindow.Screen = screen2;
    MyNewWindow.Flags  = BACKDROP | BORDERLESS;

    if (!(window2 = (struct Window *)
                        OpenWindow(&MyNewWindow)))
        Quit("Could not open the window");

    frontwindow = window1;
    ScreenToFront(screen1);

/* Set the colors for each screen to a range of red.  */

    vp = (void *) ViewPortAddress(window1);
    for (i=0; i<16; i++)
        SetRGB4(vp,i,i,0,0);

    vp = (void *) ViewPortAddress(window2);
    for (i=0; i<16; i++)
        SetRGB4(vp,i,i,0,0);

/* Initialize a TmpRas for each window.  The TmpRas   */
/* is used by the Area routines.                      */

    InitArea(&areainfo1, AreaBuffer1, 20);

    RasterBuffer1 = (void *) AllocRaster(MAXX,MAXY);
    if (!RasterBuffer1)
        Quit(NO_MEMORY);

    InitTmpRas(&tmpras1,RasterBuffer1,RASSIZE(MAXX,MAXY));

    rp = window1->RPort;
    rp->AreaInfo = &areainfo1;
    rp->TmpRas = &tmpras1;

    InitArea(&areainfo2, AreaBuffer2, 20);

    RasterBuffer2 = (void *) AllocRaster(MAXX,MAXY);
    if (!RasterBuffer2)
        Quit(NO_MEMORY);

    InitTmpRas(&tmpras2,RasterBuffer2,RASSIZE(MAXX,MAXY));

    rp = window2->RPort;
    rp->AreaInfo = &areainfo2;
    rp->TmpRas   = &tmpras2;
}




/* Shows the screen we have, presumably, just         */
/* finished drawing on.  This routine also sets       */
/* up the correct rastport, so if we always draw      */
/* into rp we never need to know the details of       */
/* which screen is in front.                          */

void SwapBuffers()
{
    rp = frontwindow->RPort;

    if (frontwindow == window1) {
        ScreenToFront(screen2);
        frontwindow = window2;
    } else {
        ScreenToFront(screen1);
        frontwindow = window1;
    }
}


/* This routine sets the field of view.  The field is */
/* specified in degrees.                              */

void Set_View_Angle(float theta)
{
    theta = fabs(theta);
    if (theta >= 180.0)
        theta = 179.9;
    else if (theta == 0.0)
        theta = 1.0;

    theta = ftan((theta / 2.0) / (180.0 / PI));

    MultX = MAXX / (ASPECT * 2.0 * theta);
    MultY = MAXY / (2.0 * theta);
}


/* After reading in all the data points, come up with */
/* some plausible default values.                     */

void SetDefaults()
{
    short       i;
    float       MinX,MaxX,
                MinY,MaxY,
                MinZ,MaxZ,
                offset;

/*  These Min and Max points define the minimum       */
/*  bounding box of all the points.  The center       */
/*  of this box will be the At point.                 */


    MinX = MaxX = World_Data[0].X;
    MinY = MaxY = World_Data[0].Y;
    MinZ = MaxZ = World_Data[0].Z;

    for (i = 1; i < TotalPoints; i++) {
        if (World_Data[i].X < MinX)
            MinX = World_Data[i].X;
        else if (World_Data[i].X > MaxX)
            MaxX = World_Data[i].X;

        if (World_Data[i].Y < MinY)
            MinY = World_Data[i].Y;
        else if (World_Data[i].Y > MaxY)
            MaxY = World_Data[i].Y;

        if (World_Data[i].Z < MinZ)
            MinZ = World_Data[i].Z;
        else if (World_Data[i].Z > MaxZ)
            MaxZ = World_Data[i].Z;
    }

/* Set the UP vector to be along the positive         */
/* z axis.                                            */

    UP.X = 0.0;
    UP.Y = 0.0;
    UP.Z = 1.0;

/* Place the At point in the middle of the            */
/* bounding box.                                      */

    At.X = (MinX + MaxX) / 2.0;
    At.Y = (MinY + MaxY) / 2.0;
    At.Z = (MinZ + MaxZ) / 2.0;


/* Put the From point just far enough away from the   */
/* At point that we could see the entire bounding box */
/* using a field of view of 100°.                     */

    offset = (MaxX - MinX) / 2.0;
    if ((MaxZ - MinZ) > (MaxY - MinY))
        offset += (MaxZ - MinZ) / 2.3;
    else
        offset += (MaxY - MinY) / 2.3;

    From.X = At.X + offset;
    From.Y = At.Y;
    From.Z = At.Z;


/* Place the Light position to the upper right of the */
/* From point                                         */

    Light.X = From.X;
    Light.Y = From.Y + offset;
    Light.Z = From.Z + offset;

/* Set a reasonable field of view                     */

    Set_View_Angle(100.0);

/* Set some reasonable light values                   */

    Ambient   = 0.3;
    Diffuse   = 0.8;
    Specular  = 0.2;     /* Not currently used        */
    Sharpness = 200.0;   /* Not currently used        */
}


/* Display a single face.  Since the Area... routines */
/* will crash if you try to draw something too large, */
/* this routine first gathers all the points into a   */
/* temporary array, checks to make sure that the      */
/* total size of the face will fit in our TmpRas, and */
/* if so it calls AreaMove, AreaDraw and AreaEnd to   */
/* draw the face.  It also sets up the area pattern   */
/* to handle dithering.                               */


void ShowFace(short n, short color)
{
    short       startpoint, nextpoint;
    short       i,pointnum;

    short       x1,x2,y1,y2,p;

    Display_Point Points[12];


/* Gather the points into the Points array.  If any   */
/* of the points are behind the viewer, don't draw    */
/* the face.                                          */

    startpoint = Face_List[n].start;
    if (Display[Connections[startpoint]].Z <= 0)
        return;

    Points[0] = Display[Connections[startpoint]];
    pointnum  = 1;

    nextpoint = Face_List[n].start + 1;

    while (nextpoint <= Face_List[n].end) {
        if (Display[Connections[nextpoint]].Z <= 0)
            return;
        Points[pointnum++] =
                     Display[Connections[nextpoint++]];
    }

/* Make sure the face fits within the TmpRas          */

    x1 = x2 = Points[0].X;
    y1 = y2 = Points[0].Y;

    for (i = 1; i < pointnum; i++) {
        p = Points[i].X;
        if (p < x1)
            x1 = p;
        else if (p > x2)
            x2 = p;

        p = Points[i].Y;
        if (p < y1)
            y1 = p;
        else if (p > y2)
            y2 = p;
    }

    if (((x2-x1) > MAXX) ||
        ((y2-y1) > MAXY) ||
        (y2 < 0)         ||
        (y1 >= MAXY)     ||
        (x2 < 0)         ||
        (x1 >= MAXX))
        return;

/* Set up the colors and patterns for dithering       */

    switch(color % 4) {

      case 0 : SetAPen(rp, color >> 2);
               SetDrMd(rp, JAM1);
               SetAfPt(rp, &SolidPattern, 0);
               break;
      case 1 : SetAPen(rp, (color >> 2)+1);
               SetBPen(rp, color >> 2);
               SetDrMd(rp, JAM2);
               SetAfPt(rp, OddCheck, 1);
               break;
      case 2 : SetAPen(rp, color >> 2);
               SetBPen(rp, (color >> 2) + 1);
               SetDrMd(rp, JAM2);
               SetAfPt(rp, EvenCheck, 1);
               break;
      case 3 : SetAPen(rp, color >> 2);
               SetBPen(rp, (color >> 2) + 1);
               SetDrMd(rp, JAM2);
               SetAfPt(rp, OddCheck, 1);
               break;
    }

/* Actually draw the face                             */

    AreaMove(rp,Points[0].X,Points[0].Y);
    for (i = 1; i < pointnum; i++)
        AreaDraw(rp, Points[i].X,Points[i].Y);
    AreaEnd(rp);
}


/* This function is used by the qsort() routine to    */
/* sort the faces from farthest to nearest.           */

int CompareFaces(Face *f1, Face *f2)
{
    if (f1->distance < f2->distance)
        return(1);
    else
        return(-1);
}

/* Display the object.  For each face, make sure the  */
/* viewer can see it.  Then figure out the color, and */
/* call ShowFace.                                     */

void ShowObject()
{
    short       i,count;
    Point_3D    Centroid,V1,V2,
                Normal,Back,L,Reflection;
    float       rcount,CenterDot,ReflectDot;

    for (i=0; i<TotalFaces; i++) {

/* Calculate the distance from the midpoint to From   */

        Centroid.X = Centroid.Y = Centroid.Z = 0.0;
        for (count = Face_List[i].start;
             count <= Face_List[i].end;
             count++)
            Minus(Centroid,
               World_Data[Connections[count]],&Centroid);
        rcount = (float)
           ((Face_List[i].start - Face_List[i].end) - 1);

        Centroid.X /= rcount;
        Centroid.Y /= rcount;
        Centroid.Z /= rcount;

        Minus(From,Centroid,&Back);

        Face_List[i].distance = DotProduct(Back,Back);
    }

/* Sort all the faces, farthest to nearest.           */

    qsort(Face_List,TotalFaces,sizeof(Face),CompareFaces);

/* Draw all the faces pointed toward us.  First,      */
/* recalculate the center of the face.                */

    for (i=0; i<TotalFaces; i++) {

        Centroid.X = Centroid.Y = Centroid.Z = 0.0;
        for (count = Face_List[i].start;
             count <= Face_List[i].end;
             count++)
            Minus(Centroid,
               World_Data[Connections[count]],&Centroid);
        rcount = (float)
           ((Face_List[i].start - Face_List[i].end) - 1);

        Centroid.X /= rcount;
        Centroid.Y /= rcount;
        Centroid.Z /= rcount;


/* Calculate the unit outer normal of the face, i.e   */
/* a vector 1 unit long that is perpendicular to the  */
/* face, and pointing outward.                        */


        count = Face_List[i].start;

        /* V1 = P3 - P1 */

        Minus(World_Data[Connections[count+2]],
              World_Data[Connections[count]],&V1);

        /* V2 = P2 - P1 */

        Minus(World_Data[Connections[count+1]],
              World_Data[Connections[count]],&V2);

        CrossProduct(V2,V1,&Normal);
        Normalize(&Normal);

/* Calculate the unit Back vector, which is a vector  */
/* 1 unit long that points from the middle of the     */
/* face toward the From point.                        */


        Minus(From,Centroid,&Back);
        Normalize(&Back);

/* If the polygon faces us, figure out the correct    */
/* color and draw it.  The dot product of two unit    */
/* vectors is the cosine of the angle between them,   */
/* and cosines of angles > 90 degrees are less than   */
/* zero (well, this is all obvious, isn't it?), so    */
/* if the dot product of the normal and the back      */
/* vectors is less than zero, the face is pointing    */
/* away from us.                                      */



        if (DotProduct(Normal,Back) > 0) {
            Minus(Light,Centroid,&L);
            Normalize(&L);
            CenterDot = DotProduct(Normal,L);
            if (CenterDot < 0.0)
                CenterDot = 0.0;


/* The specular part doesn't make much of a           */
/* contribution in the best of times, so since it     */
/* takes a while to calculate I've skipped it.        */


/*          Reflection = Normal;
            Reflection.X *= 2.0 * CenterDot;
            Reflection.Y *= 2.0 * CenterDot;
            Reflection.Z *= 2.0 * CenterDot;
            Minus(L,Reflection,&Reflection);
            ReflectDot = DotProduct(Reflection,Back);
            if (ReflectDot < 0.0)
                ReflectDot = 0.0;                     */


            count = (short) ((Ambient +
                              Diffuse * CenterDot 
          /*  + Specular * fpow(ReflectDot,Sharpness) */
                              ) * 61.0);
            if (count > 60)
                count = 60;
            ShowFace(i,count);
        }
    }
}


/* Calculate the transformation matrix V.  This       */
/* matrix depends on the From and At points as well   */
/* as the UP vector.  As long as none of these change,*/
/* you don't have to recalculate V.                   */

void Calculate_V()
{
    Point_3D  a,b,c;

    Minus(At, From, &c);
    Normalize(&c);

    CrossProduct(c, UP, &a);
    Normalize(&a);

    CrossProduct(a,c,&b);

    V[0].X = a.X;
    V[1].X = a.Y;
    V[2].X = a.Z;

    V[0].Y = b.X;
    V[1].Y = b.Y;
    V[2].Y = b.Z;

    V[0].Z = c.X;
    V[1].Z = c.Y;
    V[2].Z = c.Z;
}



/* Calculate each of the display coordinates from the */
/* world coordinates, by way of the view coordinates. */

void Compute_Display_Coords()
{
    short       i;
    Point_3D    View_Point;

    for (i=0; i<TotalPoints; i++) {
        Minus(World_Data[i],From,&View_Point);
        VectorMatrix(View_Point,V,&View_Point);

        if (View_Point.Z > 0.0) {
            Display[i].X = HALFX + (short)
                ((View_Point.X / View_Point.Z) * MultX);
            Display[i].Y = HALFY - (short)
                ((View_Point.Y / View_Point.Z) * MultY);
            Display[i].Z = 1;
        } else
            Display[i].Z = -1;
    }
}



/* Calculate everything we need to display the object */

void CalculateDisplay()
{
    Calculate_V();

    Compute_Display_Coords();
}



/* Main.  Set up some default values, then draw the   */
/* object as the From point moves around.             */

main(int argc, char *argv[])
{

    long  i;
    long  quitsignal;
    long  start,end;

    if (argc < 2)
        Quit("Usage: 3d objectfile");

    if (!(IntuitionBase = (void *)
            OpenLibrary("intuition.library",0L)))
        Quit("No Intuition");

    if (!(GfxBase = (void *)
            OpenLibrary("graphics.library",0L)))
        Quit("No Graphics");

    OpenDisplay();

    ReadObjectFile(argv[1]);

    SetDefaults();

/* The program will quit if we get any IDCMP          */
/* messages from either window.                       */

    quitsignal =(1 << window1->UserPort->mp_SigBit) |
                (1 << window2->UserPort->mp_SigBit);

    while (!(SetSignal(0,0) & quitsignal)) {

        CalculateDisplay();
        SetRast(rp, 0);
        ShowObject();
        SwapBuffers();

        RotateZ(From,At,(PI / 40.0),&From);
    }

    FreeRaster(RasterBuffer1,MAXX,MAXY);
    FreeRaster(RasterBuffer2,MAXX,MAXY);
    FreeMem(Display,TotalPoints*sizeof(Display_Point));
    FreeMem(World_Data,TotalPoints*sizeof(Point_3D));
    FreeMem(Face_List,(TotalFaces+1)*sizeof(Face));
    FreeMem(Connections,ConnectLen*sizeof(short));

    CloseWindow(window1);
    CloseScreen(screen1);
    CloseWindow(window2);
    CloseScreen(screen2);

    CloseLibrary(IntuitionBase);
    CloseLibrary(GfxBase);
}
