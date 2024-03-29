/* rtu-base contains the base types */

#include <stdint.h> 
#include <stdio.h> 

#define time_t uint32_t
/* common types */

struct InstanceLink {
  uint16_t typeID;
  uint16_t instanceID;
};

enum ValueType { booleanType, integerType, floatType, stringType, linkType, timeType };

union AnyValueType {
  bool booleanType;
  int integerType;
  double floatType;
  char* stringType;
  InstanceLink linkType;
  time_t timeType;
};

/* Well-known Resource Types, should be in a header made from the SDF translator */
  // link types for pull and push data transfer
  const uint16_t InputLinkType = 13000;
  const uint16_t OutputLinkType = 13001;
  // Value types for data connection endpoints
  const uint16_t InputValueType = 13002;
  const uint16_t CurrentValueType = 13004;
  const uint16_t OutputValueType = 13003;
  // Timer data types for wrap-around-safe interval activation 
  const uint16_t CurrentTimeType = 13005;
  const uint16_t IntervalTimeType = 13006;
  const uint16_t LastActivationTimeType = 13007;

/* base classes */

/* Resource: expose values and chain together into a linked list for each object*/
class Resource {
  public:
    uint16_t typeID;
    uint16_t instanceID;    
    Resource* nextResource;

    // Construct with type and instance + value type
    Resource(uint16_t type, uint16_t instance, ValueType vtype) {
      typeID = type;
      instanceID = instance;
      valueType = vtype;
      nextResource = NULL;
    };

    ValueType valueType;
    AnyValueType value;
};

/* Objects contain a collection of resources and some bound methods and chain into a linked list */
/* Bound methods are extended for application function types */
class Object {
  public:
    uint16_t typeID; // could be private 
    uint16_t instanceID;
    Object* nextObject; // next Object in the chain
    Object* firstObject; // first Object in the ObjectList
    Resource* firstResource; // first resource in the list for this object

    // Construct with type and instance and empty list
    Object(uint16_t type, uint16_t instance, Object* listFirstObject) {
      typeID = type;
      instanceID = instance;
      firstResource = NULL;
      nextObject = NULL;
      firstObject = listFirstObject;
    };   

    /* 
    Object state can be sync'ed (transferred from one object to another) using inputLinks
    or OutputLinks. InputLinks will cause a read operation to be performed on the linked 
    source object and the value used to set the state of this object (pull pattern). 
    Output links will cause an update operation on the linked object using the state of 
    this object (push pattern).

    When the state of objects are sync'ed, the state is copied from a default resource
    in the source object to a default resource in the destination object. The default
    source object resource is chosen by a priority ranking of 1. OutputValue, 2. CurrentValue
    and 3. InputValue. I.e. if there is no OutputValue resource, CurrentValue is chosen, etc.
    The default resource for destination object is chosen by priority ranking of
    1. InputValue, 2. CurrentValue, and 3. OutputValue

    the highest priority resource type that exists is chosen for each endpoint of the state transfer
    For Example:

    Source               Destination
    ------               -----------
    OutputValue  ======> InputValue >> Transfer is from OutputValue to InputValue
    CurrentValue         CurrentValue
    InputValue           OutputValue

    (no OutputValue)     (no InputValue)
    CurrentValue ======> CurrentValue  >> Transfer is from CurrentValue to CurrentValue 
    InputValue           OutputValue

    (no OutputValue)     (no InputValue)
    CurrentValue   ==|   (no CurrentValue)   
    (no InputValue)  |=> OutputValue  >> Transfer is from CurrentValue to OutputValue

    */

    // Copy Value from input link => this Object 
    void syncFromInputLink() {
      // readDefaultValue from InputLink
      // updateDefaultValue on this object
      Resource* inputLink = getResourceByID(InputLinkType,0);
      if (inputLink != NULL) {
        Object* sourceObject = getObjectByID(inputLink -> value.linkType.typeID, inputLink -> value.linkType.instanceID);
        updateDefaultValue(sourceObject -> onInputSync()); // call onInputSync of the source object to get dynamic values
        onValueUpdate(); // call onValueUpdate to initiate local handling of the update 
      }
    }; 

    // Copy Value from this Object => all output links 
    void syncToOutputLink() {
      // readDefaultValue from this object
      // updateDefaultValue to OutputLink(s)
     AnyValueType value = readDefaultValue();
      Resource* resource = firstResource;
        while ( (resource != NULL) ) {
          if (OutputLinkType == resource -> typeID) { // process all output links
            Object* object = getObjectByID(resource -> value.linkType.typeID, resource -> value.linkType.instanceID);
            object -> updateDefaultValue(value);
          };
        resource = resource -> nextResource;
      };
 
      if (resource != NULL) {
        //Object* object = 
        //resource -> value.linkType.typeID;
      }    
    }; 

    // Value Interface

    // Update CurrentTime value on Object and maybe call onInterval
    void updateCurrentTime(time_t timeValue) {
      // update current time
      // interval time check is wrap-safe if time variable and HW timer both have uint32_t wrap behavior
      // if(current time >= last activation time + interval time){ update last activation time and call onInterval }
      Resource* currentTime = getResourceByID(CurrentTimeType, 0);
      Resource* intervalTime = getResourceByID(IntervalTimeType, 0);
      Resource* lastActivationTime = getResourceByID(LastActivationTimeType, 0);
      currentTime -> value.timeType = timeValue;
      if (timeValue >= (lastActivationTime -> value.timeType + intervalTime -> value.timeType) ) {
        lastActivationTime -> value.timeType = timeValue;
        onInterval();
      }
    }; 
    
    // Interface to Update Value 

    void updateValueByID(uint16_t type, uint16_t instance, AnyValueType value) {
      Resource* resource = getResourceByID(type, instance);
      if (resource != NULL) {
        resource -> value = value;
      }
      else {
        printf("NULL in updateValueByID\n");
      };
    };

    void updateDefaultValue(AnyValueType value) {
      // prioritized resource types, update value and call onUpdate
      Resource* resource = getResourceByID(InputValueType,0);
      if (resource != NULL) {
        resource -> value = value;
        //onValueUpdate(resource -> typeID, resource -> instanceID, value);
        onValueUpdate();
        return;
      };
      resource = getResourceByID(CurrentValueType,0);
      if (resource != NULL) {
        resource -> value = value;
        onValueUpdate();
        return;
      };
      resource = getResourceByID(OutputValueType,0);
      if (resource != NULL){
        resource -> value = value;
        onValueUpdate();
        return;
      };
      printf("updateDefault couldn't find a candidate resource\n"); // should throw an error
      return;
    }; 

    // Interface to Read Value

    AnyValueType readValueByID(uint16_t type, uint16_t instance) {
      Resource* resource = getResourceByID(type, instance);
      AnyValueType returnValue;
      if (resource != NULL) {
        return resource -> value;
      }
      else {
        printf ("NULL in readValueByID\n");
      return(returnValue); // returns uninitialized value union if there is no candidate
      }
    }; 
    
    AnyValueType readDefaultValue() {
      AnyValueType returnValue;
      Resource* resource = getResourceByID(OutputValueType,0);
      if (resource != NULL) {
        return(resource -> value);
      };
      resource = getResourceByID(CurrentValueType,0);
      if (resource != NULL) {
        return(resource -> value);
      };
      resource = getResourceByID(InputValueType,0);
      if (resource != NULL){
        return(resource -> value);
      };
      printf("readDefault couldn't find a candidate resource\n");
      return(returnValue); // returns uninitialized value union if there is no candidate
    }; 

    // Internal Interface, implements application logic
    
    // Handler for Timer Interval
    void onInterval() {}; 

    // Handler for Value update from either input or output sync
    //void onValueUpdate(uint16_t type, uint16_t instance, AnyValueType value) {}; 
    void onValueUpdate() {}; 

    // Handler to return value in response to input sync from another object
    AnyValueType onInputSync() {
      return readDefaultValue(); // Default read value, override for e.g. gpio read
    }; 

    Resource* newResource(uint16_t type, uint16_t instance, ValueType vtype) {
      // find last resource in the chain
      if (NULL == firstResource) { // make first resource instance in the list and add to this object
        this -> firstResource = new Resource(type, instance, vtype );
        return firstResource;
      }
      else { // already have first resource
        Resource* resource = firstResource;
          while (resource -> nextResource != NULL) {
            resource = resource -> nextResource;
          };
        // make instance and add the new resource to the list
        resource -> nextResource = new Resource(type, instance, vtype );
        return resource -> nextResource;
      };
    };

    // return a pointer to the first resource in this object that matches the type and instance
    Resource* getResourceByID(uint16_t type, uint16_t instance) {
      Resource* resource = firstResource;
      while ( (resource != NULL) && (resource -> typeID != type || resource -> instanceID != instance) ) {
        resource = resource -> nextResource;
      };
      return resource; // returns NULL if resource doesn't exist
    };

    // return a pointer to the first object in the Object list that matches the type and instance
    Object* getObjectByID(uint16_t type, uint16_t instance) {
      Object* object = firstObject;
      while (object != NULL && (object -> typeID != type || object -> instanceID != instance)) {
        object = object -> nextObject;
      };
      return object; // returns NULL if doesn't exist
    };
};

class ObjectList {
  public:
    // Linked list of Objects
    Object* nextObject; // private?
    // make a new object and add it to the list
    Object* newObject(uint16_t type, uint16_t instance) {
      // find the last object in the chain, has a null nextobject pointer
      // FIXME check if it already exists?
      if (NULL == nextObject) { // make first object and add to the list (sets property of the ObjectList)
        this -> nextObject = new Object(type, instance, nextObject);
        return nextObject;
      }
      else { // already have the first object, find the end of the list 
        Object* object = nextObject;
        while (object -> nextObject != NULL) {
          object = object -> nextObject;
        };
        // make instance and add the new resource (sets property of the last Object)
        object -> nextObject = new Object(type, instance, nextObject);
        return object -> nextObject; 
      };     
    };

    // return a pointer to the first object that matches the type and instance
    Object* getObjectByID(uint16_t type, uint16_t instance) {
      Object* object = nextObject;
      while (object != NULL && (object -> typeID != type || object -> instanceID != instance)) {
        object = object -> nextObject;
      };
      return object; // returns NULL if doesn't exist
    };

    void displayObjects() {
      Object* object = nextObject;
      while ( object != NULL) {
        printf ( "[%d, %d]\n", object -> typeID, object -> instanceID);
        Resource* resource = object -> firstResource;
        while ( resource != NULL) {
          printf ( "  [%d, %d] ", resource -> typeID, resource -> instanceID);
          printf ( "value type %d, ", resource -> valueType);
          printf ( "value = ");
          switch(resource -> valueType) {
            case booleanType: {
              printf ( "%s\n", resource -> value.booleanType ? "true": "false");
              break;
            }
            case integerType: {
              printf ( "%d\n", resource -> value.integerType);
              break;
            }
            case floatType: {
              printf ( "%f\n", resource -> value.floatType);
              break;
            }
            default:
              printf ("\n");
          }
          resource = resource -> nextResource;
        };
        object = object -> nextObject;
      };
    };

    // construct with an empty object list
    ObjectList() {
      nextObject = NULL;
    };
};

int main() {
  ObjectList rtu;
  Object* object = rtu.newObject(9999,1);
  object -> newResource(1111, 1, integerType);
  object -> updateValueByID(1111, 1, (AnyValueType){.integerType=100});
  object -> newResource(2222, 2, floatType);
  object -> updateValueByID(2222, 2, (AnyValueType){.floatType=101.1});
  object = rtu.newObject(9090,2);
  object -> newResource(1010, 1, integerType);
  object -> updateValueByID(1010, 1, (AnyValueType){.integerType=1001});
  object -> newResource(1010, 2, floatType);
  object -> updateValueByID(1010, 2, (AnyValueType){.floatType=1001.01});

  rtu.displayObjects();
  return(0);
};

