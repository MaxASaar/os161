#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>
#include <thread.h>

static struct lock *intersectionLock;

volatile int nsCount;
volatile int neCount;
volatile int nwCount;
volatile int enCount;
volatile int ewCount;
volatile int esCount;
volatile int seCount;
volatile int snCount;
volatile int swCount;
volatile int wsCount;
volatile int weCount;
volatile int wnCount;

static struct cv * nsCv;
static struct cv * neCv;
static struct cv * nwCv;
static struct cv * enCv;
static struct cv * ewCv;
static struct cv * esCv;
static struct cv * seCv;
static struct cv * snCv;
static struct cv * swCv;
static struct cv * wsCv;
static struct cv * weCv;
static struct cv * wnCv;

volatile bool isActive = false;
volatile int carsInIntersection = 0;

// Checks if a car can enter the current intersection
// Should acquire the lock before calling this func
bool can_go(Direction origin, Direction destination) {
  // Straight Pathways
  if(origin == north && destination == south) {
    if(weCount > 0) return false;
    if(wnCount > 0) return false;
    if(wsCount > 0) return false;
    if(ewCount > 0) return false;
    if(esCount > 0) return false;
    if(swCount > 0) return false;
  }
  if(origin == south && destination == north) {
    if(ewCount > 0) return false;
    if(enCount > 0) return false;
    if(esCount > 0) return false;
    if(weCount > 0) return false;
    if(wnCount > 0) return false;
    if(neCount > 0) return false;
  }
  if(origin == west && destination == east) {
    if(nsCount > 0) return false;
    if(neCount > 0) return false;
    if(snCount > 0) return false;
    if(swCount > 0) return false;
    if(seCount > 0) return false;
    if(esCount > 0) return false;
  }
  if(origin == east && destination == west) {
    if(nsCount > 0) return false;
    if(neCount > 0) return false;
    if(nwCount > 0) return false;
    if(snCount > 0) return false;
    if(swCount > 0) return false;
    if(wnCount > 0) return false;
  }
  // Right Turns
  if(origin == north && destination == west) {
    if(ewCount > 0) return false;
    if(swCount > 0) return false;
  }
  if(origin == west && destination == south) {
    if(nsCount > 0) return false;
    if(esCount > 0) return false;
  }
  if(origin == south && destination == east) {
    if(weCount > 0) return false;
    if(neCount > 0) return false;
  }
  if(origin == east && destination == north) {
    if(snCount > 0) return false;
    if(wnCount > 0) return false;
  }
  // Left Turns
  if(origin == north && destination == east) {
    if(weCount > 0) return false;
    if(wnCount > 0) return false;
    if(ewCount > 0) return false;
    if(esCount > 0) return false;
    if(snCount > 0) return false;
    if(seCount > 0) return false;
    if(swCount > 0) return false;
  }
  if(origin == west && destination == north) {
    if(nsCount > 0) return false;
    if(neCount > 0) return false;
    if(ewCount > 0) return false;
    if(enCount > 0) return false;
    if(esCount > 0) return false;
    if(snCount > 0) return false;
    if(swCount > 0) return false;
  }
  if(origin == south && destination == west) {
    if(nsCount > 0) return false;
    if(nwCount > 0) return false;
    if(neCount > 0) return false;
    if(ewCount > 0) return false;
    if(esCount > 0) return false;
    if(weCount > 0) return false;
    if(wnCount > 0) return false;
  }
  if(origin == east && destination == south) {
    if(nsCount > 0) return false;
    if(neCount > 0) return false;
    if(weCount > 0) return false;
    if(wsCount > 0) return false;
    if(snCount > 0) return false;
    if(swCount > 0) return false;
    if(wnCount > 0) return false;
  }
  return true;
}

void car_signaler(void * unusedpointer, unsigned long unusedlong){
  (void) unusedpointer;
  (void) unusedlong;
  while(isActive){
    lock_acquire(intersectionLock);
    //Straight
    if(can_go(north, south)) {
      cv_broadcast(nsCv, intersectionLock);
    }
    if(can_go(west, east)) {
      cv_broadcast(weCv, intersectionLock);
    }
    if(can_go(south, north)) {
      cv_broadcast(snCv, intersectionLock);
    }
    if(can_go(east, west)) {
      cv_broadcast(ewCv, intersectionLock);
    } 
    // Right Turns
    if(can_go(north, west)) {
      cv_broadcast(nwCv, intersectionLock);
    }
    if(can_go(west, south)) {
      cv_broadcast(wsCv, intersectionLock);
    }
    if(can_go(south, east)) {
      cv_broadcast(seCv, intersectionLock);
    }
    if(can_go(east, north)) {
      cv_broadcast(enCv, intersectionLock);
    }
    // Left Turns
    if(can_go(north, east)) {
      cv_broadcast(neCv, intersectionLock);
    }
    if(can_go(west, north)) {
      cv_broadcast(wnCv, intersectionLock);
    }
    if(can_go(south, west)) {
      cv_broadcast(swCv, intersectionLock);
    }
    if(can_go(east, south)) {
      cv_broadcast(esCv, intersectionLock);
    }
    // kprintf("Please");
    lock_release(intersectionLock);
    // kprintf("Work\n");
  }
  // kprintf("WOWOWOWOWOWOWOWOWWOW");
  thread_exit();
}

/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */
void
intersection_sync_init(void)
{
  intersectionLock = lock_create("intersectionLock");
  if (intersectionLock == NULL) {
    panic("could not create intersection lock");
  }
  // Initialize the counters
  nsCount = 0;
  neCount = 0;
  nwCount = 0;
  enCount = 0;
  ewCount = 0;
  esCount = 0;
  seCount = 0;
  snCount = 0;
  swCount = 0;
  wsCount = 0;
  weCount = 0;
  wnCount = 0;
  // Initialize the cvs
  nsCv = cv_create("nsCv");
  neCv = cv_create("neCv");
  nwCv = cv_create("nwCv");
  enCv = cv_create("enCv");
  ewCv = cv_create("ewCv");
  esCv = cv_create("esCv");
  seCv = cv_create("seCv");
  snCv = cv_create("snCv");
  swCv = cv_create("swCv");
  wsCv = cv_create("wsCv");
  weCv = cv_create("weCv");
  wnCv = cv_create("wnCv");
  // Start the thread that signals cars to go if they are waiting
  isActive = true;
  int error = thread_fork("car_signaler", NULL, car_signaler, NULL, -69);
  if (error) {
    panic("car_signaler: thread_fork failed: %s\n", strerror(error));
  }
  return;
}

/* 
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
  KASSERT(intersectionLock != NULL);
  isActive = false;
  cv_destroy(nsCv);
  cv_destroy(neCv);
  cv_destroy(nwCv);
  cv_destroy(enCv);
  cv_destroy(ewCv);
  cv_destroy(esCv);
  cv_destroy(seCv);
  cv_destroy(snCv);
  cv_destroy(swCv);
  cv_destroy(wsCv);
  cv_destroy(weCv);
  cv_destroy(wnCv);
  // kprintf("cleaning up");
  lock_destroy(intersectionLock);
}


/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread 
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void
intersection_before_entry(Direction origin, Direction destination) 
{
  KASSERT(intersectionLock != NULL);
  lock_acquire(intersectionLock);
  // kprintf("e(%d,%d) ", origin, destination);
  carsInIntersection++;
  while(!can_go(origin, destination)){
    // Straight
    if(origin == north && destination == south){
      cv_wait(nsCv, intersectionLock);
    }
    if(origin == west && destination == east){
      cv_wait(weCv, intersectionLock);
    }
    if(origin == south && destination == north){
      cv_wait(snCv, intersectionLock);
    }
    if(origin == east && destination == west){
      cv_wait(ewCv, intersectionLock);
    }
    // Right Turns
    if(origin == north && destination == west){
      cv_wait(nwCv, intersectionLock);
    }
    if(origin == west && destination == south){
      cv_wait(wsCv, intersectionLock);
    }
    if(origin == south && destination == east){
      cv_wait(seCv, intersectionLock);
    }
    if(origin == east && destination == north){
      cv_wait(enCv, intersectionLock);
    }
    // Left Turns
    if(origin == north && destination == east){
      cv_wait(neCv, intersectionLock);
    }
    if(origin == west && destination == north){
      cv_wait(wnCv, intersectionLock);
    }
    if(origin == south && destination == west){
      cv_wait(swCv, intersectionLock);
    }
    if(origin == east && destination == south){
      cv_wait(esCv, intersectionLock);
    }
  }
  // Straight
  if(origin == north && destination == south) nsCount++;
  if(origin == west && destination == east) weCount++;
  if(origin == south && destination == north) snCount++;
  if(origin == east && destination == west) ewCount++;
  // Right Turns
  if(origin == north && destination == west) nwCount++;
  if(origin == west && destination == south) wsCount++;
  if(origin == south && destination == east) seCount++;
  if(origin == east && destination == north) enCount++;
  // Left Turns
  if(origin == north && destination == east) neCount++;
  if(origin == west && destination == north) wnCount++;
  if(origin == south && destination == west) swCount++;
  if(origin == east && destination == south) esCount++;
  lock_release(intersectionLock);
}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection. 
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination) 
{
  // This function acquires the intersection lock, and decrements the respective count 
  KASSERT(intersectionLock != NULL);
  lock_acquire(intersectionLock);
  // kprintf("d(%d,%d) ", origin, destination);
  carsInIntersection--;
  // kprintf("cars in intersection: %d\n", carsInIntersection);
  switch (origin)
  {
    case north:
      if(destination == east){
        neCount--;
      }
      if(destination == south){
        nsCount--;
      }
      if(destination == west){
        nwCount--;
      }
      break;
    case east:
      if(destination == south){
        esCount--;
      }
      if(destination == west){
        ewCount--;
      }
      if(destination == north){
        enCount--;
      }
      break;
    case south:
      if(destination == west){
        swCount--;
      }
      if(destination == north){
        snCount--;
      }
      if(destination == east){
        seCount--;
      }
      break;
    case west:
      if(destination == north){
        wnCount--;
      }
      if(destination == east){
        weCount--;
      }
      if(destination == south){
        wsCount--;
      }
      break;
  
    default:
      break;
  }
  lock_release(intersectionLock);
  
}
