`define SW_OFF 7'h00
`define LED_OFF 7'h04
`define BTN_OFF 7'h08

module APB_gpio #(
    // Configurable Parameters
    parameter DW = 32 ,  // Data width
    parameter AW = 32  ,  // Address width
    // Derived Parameters
    localparam CW = 1 + DW + AW ,  // Command width  {pWRITE, pWDATA, pADDR}  
    localparam RW =  DW              // Response width { pRDATA}
)(
    input logic pCLK,
    input logic pRESETn,

    // APB signals
    input logic [AW-1:0] pADDR,
    input logic  pSEL, // for each peripheral
    input logic  pENABLE,
    input logic  pWRITE,
    input logic [DW-1:0] pWDATA,
    output logic [DW-1:0] pRDATA,
    output logic  pREADY,
    output logic  pSLVERR,

    // to fpga pins
    input logic [15:0] switch,
    input logic [4:0] button,
    output logic [15:0] led
);

    // define register for GPO device 
    logic [15:0] buf_gpo;
    logic wr_en;

    // decoding logic 
    assign wr_en = pSEL & pWRITE & pREADY & pENABLE ; 
                               

    // write data into register when selected 
    always_ff @( posedge pCLK ) begin : write_logic
        if (!pRESETn) begin
            buf_gpo <= 0;
        end else begin
            if (wr_en) begin
                if(pADDR[6:0] == `LED_OFF) begin
                    buf_gpo <= pWDATA[15:0];
                end
            end
        end
    end

    assign led = buf_gpo;


    // Reading data from switches

    // define register
    logic [31:0] buf_in;
    logic rd_en;
    assign rd_en = pSEL & !pWRITE & pREADY & pENABLE ; 

    // connect to outside 
    always_comb begin
        pRDATA = 32'b0;
        if(rd_en) begin
            case (pADDR[6:0])
                `SW_OFF:  pRDATA = {16'b0, switch}; // Read switches
                `BTN_OFF: pRDATA = {27'b0, button}; // Read buttons (pad with 27 zeros)
                default:  pRDATA = 32'b0;
            endcase
        end
    end

    // Ready state logic
    assign pREADY = 1;


    
    // APB Slave Error Response
    // write fail, occurs when we write to an invalid address
    logic write_fail;
    assign write_fail = (wr_en && pADDR[6:0] != `LED_OFF) ? 1'b1 : 1'b0;

    // read fail, occurs when we read an invalid address
    logic read_fail;
    assign read_fail = ((rd_en) && (pADDR[6:0] != `SW_OFF && pADDR[6:0] != `BTN_OFF)) ? 1'b1 : 1'b0;
    
    always_ff @(posedge pCLK) begin
        if (!pRESETn) begin
            pSLVERR <= 1'b0;
        end else begin
            pSLVERR <= write_fail | read_fail;
        end
    end

endmodule