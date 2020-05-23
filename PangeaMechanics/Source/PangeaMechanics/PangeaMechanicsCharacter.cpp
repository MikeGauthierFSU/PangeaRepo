// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PangeaMechanicsCharacter.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine.h"
#include "Item.h"
#include "WeaponItem.h"

#define MAX_INVENTORY_ITEMS 4

//////////////////////////////////////////////////////////////////////////
// APangeaMechanicsCharacter

APangeaMechanicsCharacter::APangeaMechanicsCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);


	// set our turn rates for input
	BaseTurnRate = 45.f;
	BaseLookUpRate = 45.f;

	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true; // Character moves in the direction of input...	
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 540.0f, 0.0f); // ...at this rotation rate
	GetCharacterMovement()->JumpZVelocity = 600.f;
	GetCharacterMovement()->AirControl = 0.2f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 300.0f; // The camera follows at this distance behind the character	
	CameraBoom->bUsePawnControlRotation = true; // Rotate the arm based on the controller

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName); // Attach the camera to the end of the boom and let the boom adjust to match the controller orientation
	FollowCamera->bUsePawnControlRotation = false; // Camera does not rotate relative to arm

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named MyCharacter (to avoid direct content references in C++)
}

//////////////////////////////////////////////////////////////////////////
// Input

void APangeaMechanicsCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	// Set up gameplay key bindings
	check(PlayerInputComponent);
	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction("Jump", IE_Released, this, &ACharacter::StopJumping);

	// Pickup
	PlayerInputComponent->BindAction("Pickup", IE_Pressed, this, &APangeaMechanicsCharacter::BeginPickup);
	PlayerInputComponent->BindAction("Pickup", IE_Released, this, &APangeaMechanicsCharacter::EndPickup);

	// Inventory
	PlayerInputComponent->BindAction("ShowInventory", IE_Pressed, this, &APangeaMechanicsCharacter::ShowInventory);

	// Inventory
	PlayerInputComponent->BindAction("ItemInfo", IE_Pressed, this, &APangeaMechanicsCharacter::ItemInfo);

	// SLOT UP AND DOWN
	PlayerInputComponent->BindAxis("ChangeActiveSlot", this, &APangeaMechanicsCharacter::ChangeActiveSlot);

	// Drop Object
	PlayerInputComponent->BindAction("Drop", IE_Pressed, this, &APangeaMechanicsCharacter::Drop);

	// USE OBJECT
	PlayerInputComponent->BindAction("Use", IE_Pressed, this, &APangeaMechanicsCharacter::Use);

	// Move
	PlayerInputComponent->BindAxis("MoveForward", this, &APangeaMechanicsCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &APangeaMechanicsCharacter::MoveRight);

	// We have 2 versions of the rotation bindings to handle different kinds of devices differently
	// "turn" handles devices that provide an absolute delta, such as a mouse.
	// "turnrate" is for devices that we choose to treat as a rate of change, such as an analog joystick
	PlayerInputComponent->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	PlayerInputComponent->BindAxis("TurnRate", this, &APangeaMechanicsCharacter::TurnAtRate);
	PlayerInputComponent->BindAxis("LookUp", this, &APawn::AddControllerPitchInput);
	PlayerInputComponent->BindAxis("LookUpRate", this, &APangeaMechanicsCharacter::LookUpAtRate);

	// handle touch devices
	PlayerInputComponent->BindTouch(IE_Pressed, this, &APangeaMechanicsCharacter::TouchStarted);
	PlayerInputComponent->BindTouch(IE_Released, this, &APangeaMechanicsCharacter::TouchStopped);

	// VR headset functionality
	PlayerInputComponent->BindAction("ResetVR", IE_Pressed, this, &APangeaMechanicsCharacter::OnResetVR);
}


void APangeaMechanicsCharacter::OnResetVR()
{
	UHeadMountedDisplayFunctionLibrary::ResetOrientationAndPosition();
}

void APangeaMechanicsCharacter::TouchStarted(ETouchIndex::Type FingerIndex, FVector Location)
{
		Jump();
}

void APangeaMechanicsCharacter::TouchStopped(ETouchIndex::Type FingerIndex, FVector Location)
{
		StopJumping();
}

void APangeaMechanicsCharacter::TurnAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerYawInput(Rate * BaseTurnRate * GetWorld()->GetDeltaSeconds());
}

void APangeaMechanicsCharacter::LookUpAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerPitchInput(Rate * BaseLookUpRate * GetWorld()->GetDeltaSeconds());
}

void APangeaMechanicsCharacter::MoveForward(float Value)
{
	if ((Controller != NULL) && (Value != 0.0f))
	{
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		AddMovementInput(Direction, Value);
	}
}

void APangeaMechanicsCharacter::MoveRight(float Value)
{
	if ( (Controller != NULL) && (Value != 0.0f) )
	{
		// find out which way is right
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);
	
		// get right vector 
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		// add movement in that direction
		AddMovementInput(Direction, Value);
	}
}


/***************
	INVENTORY
****************/
bool APangeaMechanicsCharacter::IsInventorySlotEmpty(int &slot)
{
	bool found = false;

	for (int i = 0; i < MAX_INVENTORY_ITEMS; i++)
	{
		if (!found && StaticInventory[i] == nullptr)
		{
			found = true;
			slot = i;
		}
	}

	return found;
}

void APangeaMechanicsCharacter::ShowInventory()
{
	// Static
	for (int i = 0; i < MAX_INVENTORY_ITEMS; i++)
	{
		// Print
		if (StaticInventory[i] != nullptr)
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Blue, FString::Printf(TEXT("Item: %s"), *StaticInventory[i]->name));
		else
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Blue, FString::Printf(TEXT("Item: None")));
	}
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Blue, FString::Printf(TEXT("INVENTORY")));
}


/*************
	ITEMS 
**************/

// Pick Up Item
void APangeaMechanicsCharacter::BeginPickup()
{
	isPickingUp = true;
}

void APangeaMechanicsCharacter::EndPickup()
{
	isPickingUp = false;
}

// Drop Item
void APangeaMechanicsCharacter::Drop()
{
	FVector locToSpawn = GetActorLocation() + GetActorForwardVector() * 150.f + FVector(0, 0, 50);

	// Slot has an object
	if (StaticInventory[activeSlot] != nullptr)
	{
		StaticInventory[activeSlot]->DisableActor(false);
		StaticInventory[activeSlot]->SetActorLocation(locToSpawn);
		StaticInventory[activeSlot] = nullptr;
	}
	// Slot is empty
	else 
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("There is nothing here...")));
}

// Change Active Slot
void APangeaMechanicsCharacter::ChangeActiveSlot(float Value)
{
	// Mouse Wheel Up
	if (Value < 0)
	{
		if (activeSlot <= 0)
			activeSlot = MAX_INVENTORY_ITEMS - 1;
		else
			activeSlot--;

		// Prints
		if (StaticInventory[activeSlot] != nullptr)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, FString::Printf(TEXT("Active Item: %s"), *StaticInventory[activeSlot]->name));
		}
		else
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, FString::Printf(TEXT("Active Item: None")));

		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, FString::FromInt(activeSlot));
	}

	// Mouse Wheel Down
	else if (Value > 0)
	{
		if (activeSlot >= MAX_INVENTORY_ITEMS - 1)
			activeSlot = 0;
		else
			activeSlot++;

		// Prints
		if (StaticInventory[activeSlot] != nullptr)
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, FString::Printf(TEXT("Active Item: %s"), *StaticInventory[activeSlot]->name));
		else
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, FString::Printf(TEXT("Active Item: None")));
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, FString::FromInt(activeSlot));
	}
	
	// EQUIP ITEM
	// Weapon spawns in hand (If active slot = Weapon)
	if (Cast<AWeaponItem>(StaticInventory[activeSlot]) != nullptr)
	{
		// If has an equipped item, detach it
		if (equippedItem != nullptr) {
			equippedItem->DisableActor(true);
			equippedItem->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		}
		
		// Equip new selected Item
		equippedItem = StaticInventory[activeSlot];
		StaticInventory[activeSlot]->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, FName("socketWeapon"));
		StaticInventory[activeSlot]->DisableActor(false);
		StaticInventory[activeSlot]->SM_TBox->SetSimulatePhysics(false);
	}
	// But if active slot = non equipable item, detach actual equipped item
	else if (equippedItem != nullptr) {
		equippedItem->DisableActor(true);
		equippedItem->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		equippedItem = nullptr;
	}
}

// Use
void APangeaMechanicsCharacter::Use()
{
	if (StaticInventory[activeSlot] != nullptr)
	{
		StaticInventory[activeSlot]->Use();
		StaticInventory[activeSlot] = nullptr;
	}
	else 
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("You don't have any active item!")));
}

// ITEM INFO
void APangeaMechanicsCharacter::ItemInfo()
{
	if (StaticInventory[activeSlot] != nullptr)
	{
		StaticInventory[activeSlot]->Info();
	}
	else
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("You don't have any active item!")));
}

void APangeaMechanicsCharacter::BeginPlay()
{
	Super::BeginPlay();
}
